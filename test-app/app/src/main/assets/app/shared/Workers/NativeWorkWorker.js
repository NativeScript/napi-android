onmessage = function (msg) {
    try {
        const myUrl = new java.net.URL("http://google.com");
        postMessage("success");
    } catch (e) {
        console.log(e);
        postMessage("failed");
    }
}
