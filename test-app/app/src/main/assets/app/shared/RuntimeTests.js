describe(module.id, function () {
    it('Runtime version', function () {
        expect(__runtimeVersion).toBeDefined();
    });
});

if (global.NSObject) { // platform is iOS
    describe("Unicode tests", function () {
        it("Get unicode property", function () {
            var obj = NSObject.alloc().init();
            obj.Ł = "Ł"
            expect(obj.Ł).toBe("Ł");
        });
    });
}

