use super::{index::is_digest, IndexedEvidence};
use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int, c_void};
use std::path::Path;

const SQLITE_OK: c_int = 0;
const SQLITE_ROW: c_int = 100;
const SQLITE_DONE: c_int = 101;
const MAX_RECORDS: usize = 10_000;

type Sqlite3 = c_void;
type Sqlite3Stmt = c_void;

fn sqlite_transient() -> unsafe extern "C" fn(*mut c_void) {
    unsafe { std::mem::transmute(-1isize) }
}

#[link(name = "sqlite3")]
unsafe extern "C" {
    fn sqlite3_open(filename: *const c_char, database: *mut *mut Sqlite3) -> c_int;
    fn sqlite3_close(database: *mut Sqlite3) -> c_int;
    fn sqlite3_errmsg(database: *mut Sqlite3) -> *const c_char;
    fn sqlite3_exec(database: *mut Sqlite3, sql: *const c_char, callback: Option<unsafe extern "C" fn()>, argument: *mut c_void, error: *mut *mut c_char) -> c_int;
    fn sqlite3_free(pointer: *mut c_void);
    fn sqlite3_prepare_v2(database: *mut Sqlite3, sql: *const c_char, length: c_int, statement: *mut *mut Sqlite3Stmt, tail: *mut *const c_char) -> c_int;
    fn sqlite3_finalize(statement: *mut Sqlite3Stmt) -> c_int;
    fn sqlite3_bind_text(statement: *mut Sqlite3Stmt, index: c_int, value: *const c_char, length: c_int, destructor: unsafe extern "C" fn(*mut c_void)) -> c_int;
    fn sqlite3_bind_int64(statement: *mut Sqlite3Stmt, index: c_int, value: i64) -> c_int;
    fn sqlite3_step(statement: *mut Sqlite3Stmt) -> c_int;
    fn sqlite3_column_text(statement: *mut Sqlite3Stmt, column: c_int) -> *const c_char;
    fn sqlite3_column_int64(statement: *mut Sqlite3Stmt, column: c_int) -> i64;
}

pub struct SqliteEvidenceStore { database: *mut Sqlite3 }

impl SqliteEvidenceStore {
    pub fn open(path: &Path) -> Result<Self, String> {
        let path = CString::new(path.to_string_lossy().as_bytes()).map_err(|_| "invalid database path".to_owned())?;
        let mut database = std::ptr::null_mut();
        let status = unsafe { sqlite3_open(path.as_ptr(), &mut database) };
        if status != SQLITE_OK { return Err(Self::error(database)); }
        let store = Self { database };
        store.execute("CREATE TABLE IF NOT EXISTS evidence (evidence_id TEXT PRIMARY KEY, case_id TEXT NOT NULL, content_sha256 TEXT NOT NULL, content_length INTEGER NOT NULL, media_type TEXT NOT NULL, source_path TEXT NOT NULL)")?;
        Ok(store)
    }

    fn error(database: *mut Sqlite3) -> String {
        if database.is_null() { return "sqlite open failed".to_owned(); }
        unsafe { CStr::from_ptr(sqlite3_errmsg(database)).to_string_lossy().into_owned() }
    }

    fn execute(&self, sql: &str) -> Result<(), String> {
        let sql = CString::new(sql).map_err(|_| "invalid SQL".to_owned())?;
        let mut error = std::ptr::null_mut();
        let status = unsafe { sqlite3_exec(self.database, sql.as_ptr(), None, std::ptr::null_mut(), &mut error) };
        if status == SQLITE_OK { return Ok(()); }
        let message = if error.is_null() { Self::error(self.database) } else { unsafe { CStr::from_ptr(error).to_string_lossy().into_owned() } };
        if !error.is_null() { unsafe { sqlite3_free(error.cast()); } }
        Err(message)
    }

    pub fn upsert(&self, record: &IndexedEvidence) -> Result<(), String> {
        if record.evidence_id.is_empty() || !is_digest(&record.content_sha256) || !record.source_path.starts_with('/') { return Err("invalid evidence record".to_owned()); }
        let count = self.count()?;
        if count >= MAX_RECORDS && self.get(&record.evidence_id)?.is_none() { return Err("evidence index capacity exceeded".to_owned()); }
        let sql = CString::new("INSERT INTO evidence (evidence_id, case_id, content_sha256, content_length, media_type, source_path) VALUES (?, ?, ?, ?, ?, ?) ON CONFLICT(evidence_id) DO UPDATE SET case_id=excluded.case_id, content_sha256=excluded.content_sha256, content_length=excluded.content_length, media_type=excluded.media_type, source_path=excluded.source_path").map_err(|_| "invalid SQL".to_owned())?;
        let mut statement = std::ptr::null_mut();
        let status = unsafe { sqlite3_prepare_v2(self.database, sql.as_ptr(), -1, &mut statement, std::ptr::null_mut()) };
        if status != SQLITE_OK { return Err(Self::error(self.database)); }
        let values = [&record.evidence_id, &record.case_id, &record.content_sha256, &record.media_type, &record.source_path];
        for (index, value) in values.iter().enumerate() {
            let value = CString::new(value.as_bytes()).map_err(|_| "invalid evidence text".to_owned())?;
            let bind_status = unsafe { sqlite3_bind_text(statement, (index + 1) as c_int, value.as_ptr(), -1, sqlite_transient()) };
            if bind_status != SQLITE_OK { unsafe { sqlite3_finalize(statement); } return Err(Self::error(self.database)); }
        }
        let bind_status = unsafe { sqlite3_bind_int64(statement, 4, record.content_length as i64) };
        if bind_status != SQLITE_OK { unsafe { sqlite3_finalize(statement); } return Err(Self::error(self.database)); }
        // content_length occupies parameter four; media and source are parameters five and six.
        for (index, value) in [&record.media_type, &record.source_path].iter().enumerate() {
            let value = CString::new(value.as_bytes()).map_err(|_| "invalid evidence text".to_owned())?;
            let bind_status = unsafe { sqlite3_bind_text(statement, (index + 5) as c_int, value.as_ptr(), -1, sqlite_transient()) };
            if bind_status != SQLITE_OK { unsafe { sqlite3_finalize(statement); } return Err(Self::error(self.database)); }
        }
        let step = unsafe { sqlite3_step(statement) };
        unsafe { sqlite3_finalize(statement); }
        if step == SQLITE_DONE { Ok(()) } else { Err(Self::error(self.database)) }
    }

    fn count(&self) -> Result<usize, String> {
        let records = self.load_all()?;
        Ok(records.len())
    }

    pub fn get(&self, evidence_id: &str) -> Result<Option<IndexedEvidence>, String> {
        Ok(self.load_all()?.into_iter().find(|record| record.evidence_id == evidence_id))
    }

    pub fn load_all(&self) -> Result<Vec<IndexedEvidence>, String> {
        let sql = CString::new("SELECT evidence_id, case_id, content_sha256, content_length, media_type, source_path FROM evidence ORDER BY evidence_id").map_err(|_| "invalid SQL".to_owned())?;
        let mut statement = std::ptr::null_mut();
        if unsafe { sqlite3_prepare_v2(self.database, sql.as_ptr(), -1, &mut statement, std::ptr::null_mut()) } != SQLITE_OK { return Err(Self::error(self.database)); }
        let mut records = Vec::new();
        loop {
            let step = unsafe { sqlite3_step(statement) };
            if step == SQLITE_DONE { break; }
            if step != SQLITE_ROW { unsafe { sqlite3_finalize(statement); } return Err(Self::error(self.database)); }
            if records.len() == MAX_RECORDS { unsafe { sqlite3_finalize(statement); } return Err("evidence index capacity exceeded".to_owned()); }
            let text = |column: c_int| unsafe { CStr::from_ptr(sqlite3_column_text(statement, column)).to_string_lossy().into_owned() };
            records.push(IndexedEvidence { evidence_id: text(0), case_id: text(1), content_sha256: text(2), content_length: unsafe { sqlite3_column_int64(statement, 3) as u64 }, media_type: text(4), source_path: text(5) });
        }
        unsafe { sqlite3_finalize(statement); }
        Ok(records)
    }
}

impl Drop for SqliteEvidenceStore { fn drop(&mut self) { if !self.database.is_null() { unsafe { sqlite3_close(self.database); } } } }

unsafe impl Send for SqliteEvidenceStore {}
unsafe impl Sync for SqliteEvidenceStore {}
