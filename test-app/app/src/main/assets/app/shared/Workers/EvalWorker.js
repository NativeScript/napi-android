onmessage = function(msg) {
    var value = msg.data.value;

    eval(`
        globalThis.localEval = function(value) {
            ${msg.data.eval || ""}
        }
    `);

    globalThis.localEval(value);

}