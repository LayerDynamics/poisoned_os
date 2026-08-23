use poisoned_bridge::channel::{ChannelError, ChannelState, ReceiveResult};

#[test]
fn credit_window_and_sequence_rules_match_firmware_contract() {
    let mut channel = ChannelState::new("device", 1).expect("valid channel");
    assert_eq!(channel.reserve_send(32), Ok(0));
    assert_eq!(channel.reserve_send(32), Err(ChannelError::NoCredit));
    assert_eq!(channel.receive(32, 1), Ok(ReceiveResult::Gap));
    assert_eq!(channel.receive(32, 0), Ok(ReceiveResult::Accepted));
    assert_eq!(channel.receive(32, 0), Ok(ReceiveResult::Duplicate));
}

#[test]
fn frame_limit_and_close_are_enforced() {
    let mut channel = ChannelState::new("device", 0).expect("valid channel");
    assert_eq!(channel.receive(1025, 0), Err(ChannelError::Invalid));
    channel.close();
    assert_eq!(channel.receive(1, 0), Err(ChannelError::Closed));
}

