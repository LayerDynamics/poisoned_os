use poison_runtime::{invoke, Exit};
use poison_sdk::Status;

extern "C" fn entry() -> Status { Status::OK }

#[test]
fn successful_entry_releases_resources() {
    assert_eq!(invoke(entry), Exit { status: Status::OK, resources_released: true });
}
