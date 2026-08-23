use poison_sdk::{ByteSlice, Status, ABI_VERSION, API_VERSION};

#[test]
fn abi_contract_is_stable() {
    assert_eq!(API_VERSION, 1);
    assert_eq!(ABI_VERSION, 1);
    assert_eq!(core::mem::size_of::<Status>(), 4);
    assert_eq!(core::mem::size_of::<ByteSlice>(), core::mem::size_of::<usize>() * 2);
}
