CREATE TABLE evidence_index (
    evidence_id TEXT PRIMARY KEY,
    case_id TEXT NOT NULL,
    content_sha256 TEXT NOT NULL,
    content_length INTEGER NOT NULL,
    media_type TEXT NOT NULL,
    source_path TEXT NOT NULL
);
