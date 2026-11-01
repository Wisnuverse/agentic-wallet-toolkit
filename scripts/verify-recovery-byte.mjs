// verify-recovery-byte.mjs — EIP-712 signature recovery-byte checker.
//
// Some wallet signing flows return a 64-byte signature plus a separate
// recovery flag ("00" / "01") instead of a full 65-byte signature. Appending
// the wrong byte ("1b" instead of "1c", or vice versa) yields a signature
// that fails verification on the server side.
//
// This tool recovers the signer address for BOTH candidate bytes and reports
// which one matches the expected wallet — locally, before you burn a login
// attempt or a one-shot submission.
//
// Usage:
//   node verify-recovery-byte.mjs <typed-data.json> <signature-hex-64-bytes> <expected-wallet>
//
// typed-data.json: an EIP-712 object { domain, types, primaryType, message }.
// Numeric message fields that the signing flow encoded as integers should be
// numbers in the JSON; convert to BigInt before calling your own verifier if
// your types demand it — viem accepts numbers for uint fields in most cases.
//
// Requires: npm install viem

import { recoverTypedDataAddress } from 'viem';
import { readFileSync } from 'fs';

const [, , typedDataPath, sigHexRaw, expectedWallet] = process.argv;
if (!typedDataPath || !sigHexRaw || !expectedWallet) {
  console.error('Usage: node verify-recovery-byte.mjs <typed-data.json> <64-byte-signature-hex> <expected-wallet>');
  process.exit(2);
}

const typedData = JSON.parse(readFileSync(typedDataPath, 'utf8'));
const sig64 = sigHexRaw.trim().replace(/^0x/, '');
if (!/^[0-9a-fA-F]{128}$/.test(sig64)) {
  console.error('Signature must be exactly 64 bytes (128 hex chars), without the recovery byte.');
  process.exit(2);
}

let matched = null;
for (const v of ['1b', '1c']) {
  const signature = '0x' + sig64 + v;
  try {
    const addr = await recoverTypedDataAddress({ ...typedData, signature });
    const isMatch = addr.toLowerCase() === expectedWallet.toLowerCase();
    console.log(`recovery byte ${v}: recovered ${addr}${isMatch ? '  <-- MATCH' : ''}`);
    if (isMatch) matched = v;
  } catch (e) {
    console.log(`recovery byte ${v}: ERR ${e.message.slice(0, 100)}`);
  }
}

if (matched) {
  console.log(`\nUse recovery byte ${matched}: full signature is 0x${sig64}${matched}`);
} else {
  console.log('\nNeither byte recovered the expected wallet. Check the typed data and signature.');
  process.exit(1);
}
