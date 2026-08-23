use poisoned_bridge::auth::{AuthError, OriginAuthenticator};

#[test]
fn origin_token_is_bound_to_loopback_origin() {
    let auth = OriginAuthenticator::new("http://127.0.0.1:49000", "secret").unwrap();
    assert_eq!(auth.authorize(None, Some("secret")), Err(AuthError::MissingOrigin));
    assert_eq!(auth.authorize(Some("http://localhost:49000"), Some("secret")), Err(AuthError::InvalidOrigin));
    assert_eq!(auth.authorize(Some("http://127.0.0.1:49000"), Some("bad")), Err(AuthError::InvalidToken));
    assert!(auth.authorize(Some("http://127.0.0.1:49000"), Some("secret")).is_ok());
}

#[test]
fn websocket_subprotocol_carries_the_exact_origin_token() {
    let auth = OriginAuthenticator::new("http://127.0.0.1:49000", "secret").unwrap();
    let protocol = auth
        .authorize_websocket(
            Some("http://127.0.0.1:49000"),
            Some("chat, poisoned-os.rpc.v1.secret"),
        )
        .unwrap();
    assert_eq!(protocol, "poisoned-os.rpc.v1.secret");
    assert_eq!(
        auth.authorize_websocket(
            Some("http://127.0.0.1:49000"),
            Some("poisoned-os.rpc.v1.wrong"),
        ),
        Err(AuthError::InvalidToken),
    );
    assert_eq!(
        auth.authorize_websocket(
            Some("http://localhost:49000"),
            Some("poisoned-os.rpc.v1.secret"),
        ),
        Err(AuthError::InvalidOrigin),
    );
}
