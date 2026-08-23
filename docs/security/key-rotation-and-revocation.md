# Key Rotation and Revocation

Release, package, pairing, and evidence authorities are rotated by publishing a new signed authority record, distributing the revocation set, and verifying denial of the prior key before promotion. A rotation is incomplete until the old key is rejected and the replacement verifies on a clean local profile.

The release manifest and revocation verifier remain the machine-checked authority. No hosted service is required for local verification.
