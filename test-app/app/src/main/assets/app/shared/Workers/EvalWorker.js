onmessage = function(msg) {
    // Expose `value` as a global so the eval'd snippet can reference it. A local
    // `var value` is invisible to direct eval on engines that don't support
    // lexical scope for eval (e.g. static Hermes); a global resolves everywhere.
    globalThis.value = msg.data.value;
    eval(msg.data.eval || "");
}