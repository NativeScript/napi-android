'use strict';

// PrimJS adapter. PrimJS is QuickJS-derived with a LEPUS_* API, so it uses its
// own shim (native/primjs-compile.c) but the same container format. The runtime
// reads this container in napi/primjs js_execute_script (strip the 12-byte
// header, LEPUS_ReadObject + LEPUS_EvalFunction); see quickjs.js.
const MAGIC = Buffer.from('NSBCPJS\0', 'latin1'); // 8 bytes, must match the shim + runtime

module.exports = {
  key: 'primjs',
  engineKeys: ['PRIMJS'],
  ready: true,
  magic: MAGIC,
  supportsSourceMaps: false,
  defaultOptimize: null,

  binName(hostKey) {
    return hostKey.startsWith('win32') ? 'nsbc-primjs.exe' : 'nsbc-primjs';
  },

  isBytecode(head) {
    return head.length >= MAGIC.length && head.subarray(0, MAGIC.length).equals(MAGIC);
  },

  buildArgs({ input, output }) {
    return [input, output];
  },

  sourceMapOutput(output) {
    return output + '.map';
  },
};
