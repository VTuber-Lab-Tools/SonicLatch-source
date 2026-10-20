# Windows installer fixtures

These development fixtures exercise transaction, ownership and recovery behavior. A fixture pass is separate from acceptance of normal Setup, native uninstall or a particular machine state. Keep test receipts and workstation logs in private evidence, outside corresponding source.

## Three initial filesystem states

| State | Initial disposable Windows environment | Record before running |
|---|---|---|
| A | ProgramData/obs-studio does not exist | Clean isolated VM snapshot; OBS closed; exact Setup hash |
| B | A standard user created obs-studio/plugins normally | Existing owner, DACL and plugins file identity |
| C | An administrator created plugins through normal Explorer use | Owner, DACL and inherited ProgramData permissions |

For normal Setup acceptance in each state, launch the reviewed Setup normally and let the user respond to standard UAC. Do not make ACL modification scripts part of installation instructions. Record payload hashes, receipt/journal, seven HKLM64 registration values and plugins identity. Check that native uninstall removes only owned payload/registration while retaining unknown files, other plugins and OBS settings. Parent owner/DACL must remain unchanged. Treat reparse points, hardlinks and modified management files as separate refusal cases.

Report payload application, receipt commit, management publication, NSIS handoff and registration as separate stages. A completed transaction does not prove every subsequent stage. Update user instructions to the final observed behavior rather than historical development findings.

## Native filesystem fixture

The fixture uses unchanged copies of the WindowsStore/Bootstrap/Engine headers, replacing SHGetKnownFolderPath only within the test translation unit. It does not add a configurable product root to the shipping helper. Its dummy payload is not an executable DLL. It does not connect to product registration, NSIS or installed application settings.

The scenario creates a fresh fixture for each state, injects an interruption after the first payload publication, recovers, reapplies with a new transaction, verifies that committed recovery does not roll back the payload, and removes owned files using a new transaction. Keep journal/plan/blob evidence. Compare parent owner/DACL before and after.

Run only inside a disposable isolated Windows VM. The explicit command creates a fresh fixture under `C:/SonicLatch-installer-QA`; unsafe ACL/path conditions cause refusal and must not be weakened. A normal administrator token is required. Do not run this fixture against a production host. With no arguments the program prints usage without creating a fixture.

```text
SonicLatch-three-state-fixture.exe --run-admin-fixtures --isolated-vm
```

This exercises native filesystem transactions. It does not establish normal Setup acceptance in all three states. Do not install VM software or change operating-system features merely to run this fixture without separate authorization.

## Recovery and evidence interpretation

`recover-active` preserves an already committed transaction and resumes its management stage; it is not a command to restore a prior release. An upgrade requires an authenticated acceptedPrevious catalog entry for the installed predecessor. Restoring earlier payload requires an independently authenticated reversible transition. Never fabricate recovery by deleting a journal or receipt.

Distinguish artificial sources from real microphones. Record source type, timestamp mapping, filter configuration and signal origin for audio tests. A WASAPI module being present in an OBS log does not prove that a real WASAPI microphone supplied the measured samples.

## TxR fixture

`SonicLatch-txr-native-fixture` is separate from normal Setup. Its intended scope is a disposable HKCU key and explicit interruption points. Native TxR availability and the user-hive transaction resource manager can prevent even a parent-key open; record the actual API result rather than treating that as a completed transaction. Do not silently substitute a nontransactional registration path. Read-only probes, UAC prompts and normal Setup acceptance are separate observations.
