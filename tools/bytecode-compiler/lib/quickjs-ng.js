'use strict';

// QuickJS-NG adapter. Same JS_* API and shim as QuickJS (native/qjs-compile.c),
// but a distinct engine whose bytecode is NOT interchangeable — hence its own
// magic and bin/quickjs-ng/ directory. The runtime reads this container in
// napi/quickjs js_execute_script (the __QUICKJS_NG__ build); see quickjs.js.
const MAGIC = Buffer.from('NSBCNGS\0', 'latin1'); // 8 bytes, must match the shim + runtime

module.exports = {
  key: 'quickjs-ng',
  engineKeys: ['QUICKJS_NG'],
  ready: true,
  magic: MAGIC,
  supportsSourceMaps: false,
  defaultOptimize: null,

  binName(hostKey) {
    return hostKey.startsWith('win32') ? 'nsbc-quickjs-ng.exe' : 'nsbc-quickjs-ng';
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
