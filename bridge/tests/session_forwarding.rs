use poisoned_bridge::device::{DeviceRegistry, DeviceError, DeviceTransport, DeviceDescriptor};

#[tokio::test]
async fn one_transport_owner_is_enforced_and_sessions_are_cleaned_up() {
    let registry = DeviceRegistry::new(vec![DeviceDescriptor { id: "f7-1".into(), label: "Flipper".into(), transport: DeviceTransport::Usb }], 4);
    let first = registry.open_session("f7-1").await.unwrap();
    assert_eq!(registry.open_session("f7-1").await, Err(DeviceError::Busy));
    assert!(registry.session(first.id).await.is_some());
    registry.close_session(first.id).await.unwrap();
    assert!(registry.session(first.id).await.is_none());
    assert!(registry.open_session("f7-1").await.is_ok());
    assert!(registry.close_session(first.id).await.is_err());
    assert_eq!(registry.open_session("missing").await, Err(DeviceError::NotFound));
}
