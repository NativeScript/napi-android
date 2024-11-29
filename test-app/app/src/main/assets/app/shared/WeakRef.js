xdescribe("WeakRef", function () {
    it("should exist", function () {
        expect(WeakRef).toBeDefined();
    });

    it("should work", function (done) {
        var obj = {};
        var weakref = new WeakRef(obj);

        obj = null;
        __collect();

        setTimeout(() => {
            expect(weakref.get()).toBe(null);
            expect(obj).toBe(null);
            done();
        }, 10);
    });

    it("should throw when constructed with zero parameters", function () {
        expect(function () {
            new WeakRef();
        }).toThrow();
    });

    it("should throw when constructed with primitive parameters", function () {
        for (var primitive of [null, undefined, 0]) {
            expect(function () {
                new WeakRef(primitive);
            }).toThrow();
        }
    });

    it("should be clearable", function () {
        var obj = {};
        var weakref = new WeakRef(obj);

        weakref.clear();

        expect(weakref.get()).toBeNull();
    });

    if(global.NSObject) { // platform is iOS
        it("exceptions", function() {
            expect(() => new WeakRef()).toThrowError(/undefined must be an object \(evaluating 'new WeakRef\(\)'\)/);
            expect(() => WeakRef.prototype.get.apply({})).toThrowError(/Object 'this' is not weak reference \(evaluating 'WeakRef.prototype.get.apply\({}\)'\)/);
            expect(() => WeakRef.prototype.clear.apply(1)).toThrowError(/1 'this' is not weak reference \(evaluating 'WeakRef.prototype.clear.apply\(1\)'\)/);
        });
     }
});
