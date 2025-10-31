(function () {
  var __decorate =
    (this && this.__decorate) ||
    function (decorators, target, key, desc) {
      var c = arguments.length;
      var r =
          c < 3
            ? target
            : desc === null
            ? (desc = Object.getOwnPropertyDescriptor(target, key))
            : desc,
        d;

      if (
        typeof global.Reflect === "object" &&
        typeof global.Reflect.decorate === "function"
      ) {
        r = global.Reflect.decorate(decorators, target, key, desc);
      } else {
        for (var i = decorators.length - 1; i >= 0; i--) {
          if ((d = decorators[i])) {
            r =
              (c < 3 ? d(r) : c > 3 ? d(target, key, r) : d(target, key)) || r;
          }
        }
      }
      return c > 3 && r && Object.defineProperty(target, key, r), r;
    };

  // For backward compatibility.
  var __native = function (thiz) {
    // we are setting the __container__ property to the base class when the super method is called
    // if the constructor returns the __native(this) call we will use the old implementation
    // copying all the properties to the result
    // otherwise if we are using the result from the super() method call we won't need such logic
    // as thiz already contains the parent properties
    // this way we now support both implementations in typescript generated constructors:
    // 1: super(); return __native(this);
    // 2: return super() || this;
    //    if (thiz.__container__) {
    //    if (__useHostObjects) {
    //    for (var prop in thiz) {
    //            if (thiz.hasOwnProperty(prop)) {
    //              thiz.__proto__[prop] = thiz[prop];
    //              delete thiz[prop];
    //            }
    //       }
    //    }

    return thiz;
    //    } else {
    //      return thiz;
    //    }
  };

  var __extends = function (Child, Parent) {
    var extendNativeClass =
      !!Parent.extend && Parent.extend.toString().indexOf("[native code]") > -1;
    if (!extendNativeClass) {
      __extends_ts(Child, Parent);
      return;
    }
    if (Parent.__isPrototypeImplementationObject) {
      throw new Error("Can not extend an already extended native object.");
    }

    function extend(thiz) {
      var child = thiz.__proto__.__child;
      if (!child.__extended) {
        var parent = thiz.__proto__.__parent;
        child.__extended = parent.extend(child.name, child.prototype, true);
        // This will deal with "i instanceof child"
        child[Symbol.hasInstance] = function (instance) {
          return instance instanceof this.__extended;
        };
      }
      return child.__extended;
    }

    Parent.__activityExtend = function (parent, name, implementationObject) {
      __log("__activityExtend called");
      return parent.extend(name, implementationObject);
    };

    Parent.call = function (thiz) {
      var Extended = extend(thiz);
      thiz.__container__ = true;
      if (arguments.length > 1) {
        if (typeof Extended !== "function") {
          thiz = Reflect.construct(
            Extended,
            Array.prototype.slice.call(arguments, 1)
          );
        } else {
          thiz = new (Function.prototype.bind.apply(
            Extended,
            [null].concat(Array.prototype.slice.call(arguments, 1))
          ))();
        }
      } else {
        thiz = new Extended();
      }
      return thiz;
    };

    Parent.apply = function (thiz, args) {
      var Extended = extend(thiz);
      thiz.__container__ = true;
      if (args && args.length > 0) {
        if (typeof Extended !== "function") {
          thiz = Reflect.construct(Extended, [null].concat(args));
        } else {
          thiz = new (Function.prototype.bind.apply(
            Extended,
            [null].concat(args)
          ))();
        }
      } else {
        thiz = new Extended();
      }
      return thiz;
    };
    __extends_ns(Child, Parent);
    Child.__isPrototypeImplementationObject = true;
    Child.__proto__ = Parent;
    Child.prototype.__parent = Parent;
    Child.prototype.__child = Child;
  };

  var __extends_ts = function (child, parent) {
    extendStaticFunctions(child, parent);
    assignPrototypeFromParentToChild(parent, child);
  };

  var __extends_ns = function (child, parent) {
    if (!parent.extend) {
      assignPropertiesFromParentToChild(parent, child);
    }

    assignPrototypeFromParentToChild(parent, child);
  };

  var extendStaticFunctions =
    Object.setPrototypeOf ||
    (hasInternalProtoProperty() &&
      function (child, parent) {
        child.__proto__ = parent;
      }) ||
    assignPropertiesFromParentToChild;

  function hasInternalProtoProperty() {
    return { __proto__: [] } instanceof Array;
  }

  function assignPropertiesFromParentToChild(parent, child) {
    for (var property in parent) {
      if (parent.hasOwnProperty(property)) {
        child[property] = parent[property];
      }
    }
  }

  function assignPrototypeFromParentToChild(parent, child) {
    function __() {
      this.constructor = child;
    }

    if (parent === null) {
      child.prototype = Object.create(null);
    } else {
      __.prototype = parent.prototype;
      child.prototype = new __();
    }
  }

  function JavaProxy(className) {
    return function (target) {
      var extended = target.extend(className, target.prototype);
      extended.name = className;
      return extended;
    };
  }

  function Interfaces(interfacesArr) {
    return function (target) {
      if (interfacesArr instanceof Array) {
        // attach interfaces: [] to the object
        target.prototype.interfaces = interfacesArr;
      }
    };
  }

  Object.defineProperty(global, "__native", { value: __native });
  Object.defineProperty(global, "__extends", { value: __extends });
  Object.defineProperty(global, "__decorate", { value: __decorate });

  if (!global.__ns__worker) {
    global.JavaProxy = JavaProxy;
  }
  global.Interfaces = Interfaces;

  if (global.WeakRef && !global.WeakRef.prototype.get) {
    global.WeakRef.prototype.get = global.WeakRef.prototype.deref;
  }

  global.setNativeArrayProp = (target, prop, value, receiver) => {
    if (typeof prop !== "symbol" && !isNaN(prop)) {
      receiver.setValueAtIndex(parseInt(prop), value);
      return true;
    }
    target[prop] = value;
    return true;
  };

  global.getNativeArrayProp = (target, prop, receiver) => {
    if (typeof prop !== "symbol" && !isNaN(prop)) {
      return receiver.getValueAtIndex(parseInt(prop));
    }

    if (prop === Symbol.iterator) {
      var index = 0;
      const l = target.length;
      return function () {
        return {
          next: function () {
            if (index < l) {
              return {
                value: receiver.getValueAtIndex(index++),
                done: false,
              };
            } else {
              return { done: true };
            }
          },
        };
      };
    }
    if (prop === "map") {
      return function (callback) {
        const values = receiver.getAllValues();
        const result = [];
        const l = target.length;
        for (var i = 0; i < l; i++) {
          result.push(callback(values[i], i, target));
        }
        return result;
      };
    }

    if (prop === "toString") {
      return function () {
        const result = receiver.getAllValues();
        return result.join(",");
      };
    }

    if (prop === "forEach") {
      return function (callback) {
        const values = receiver.getAllValues();
        const l = values.length;
        for (var i = 0; i < l; i++) {
          callback(values[i], i, target);
        }
      };
    }
    return target[prop];
  };

  function findInPrototypeChain(obj, prop) {
    while (obj) {
      if (obj.hasOwnProperty(prop)) {
        return Object.getOwnPropertyDescriptor(obj, prop);
      }
      obj = Object.getPrototypeOf(obj);
    }
    return undefined;
  }

  globalThis.__prepareHostObject = function (hostObject, jsThis) {
    //    const prototype = Object.getPrototypeOf(jsThis);
    //    Object.setPrototypeOf(hostObject, prototype);
    Object.defineProperty(hostObject, "super", {
      get: () => jsThis["super"],
    });
  };

  const EXTERNAL_PROP = "[[external]]";
  const REFERENCE_PROP_JSC = "[[jsc_reference_info]]";

  function __createNativeProxy(object, objectId) {
    const proxy = new Proxy(object, {
      get: function (target, prop) {
        if (prop === EXTERNAL_PROP) return this[EXTERNAL_PROP];
        if (prop === REFERENCE_PROP_JSC) return this[REFERENCE_PROP_JSC];
        if (target.__is__javaArray) {
          return global.getNativeArrayProp(target, prop, target);
        }
        return target[prop];
      },
      set: function (target, prop, value) {
        if (prop === EXTERNAL_PROP) {
          this[EXTERNAL_PROP] = value;
          return true;
        }

        if (prop === REFERENCE_PROP_JSC) {
          this[REFERENCE_PROP_JSC] = value;
        }

        if (target.__is__javaArray && !isNaN(prop)) {
          target.setValueAtIndex(parseInt(prop), value);
          return true;
        }

        target[prop] = value;
        return true;
      },
    });
    return proxy;
  }
  globalThis.__createNativeProxy = __createNativeProxy;

  globalThis.getErrorStack = (err) => {
    if (err) return err.stack;
    const stack = new Error("").stack;
    const lines = stack.split("\n");
    // Line 2 results in invalid stack if not replaced when doing typescript extend.
    lines[2] = "  at extend(native)";
    return lines.join("\n");
  };

  if (globalThis.URL) {
    const BLOB_STORE = new Map();
    URL.createObjectURL = function (object, options = null) {
      try {
        if (object instanceof Blob || object instanceof File) {
          const id = java.util.UUID.randomUUID().toString();
          const ret = `blob:nativescript/${id}`;
          BLOB_STORE.set(ret, {
            blob: object,
            type: object?.type,
            ext: options?.ext,
          });
          return ret;
        }
      } catch (error) {
        return null;
      }
      return null;
    };
    URL.revokeObjectURL = function (url) {
      BLOB_STORE.delete(url);
    };
    function InternalAccessor() {}
    InternalAccessor.getData = function (url) {
      return BLOB_STORE.get(url);
    };
    URL.InternalAccessor = InternalAccessor;
    Object.defineProperty(URL.prototype, "searchParams", {
      get() {
        if (this._searchParams == null) {
          this._searchParams = new URLSearchParams(this.search);
          Object.defineProperty(this._searchParams, "_url", {
            enumerable: false,
            writable: false,
            value: this,
          });
          this._searchParams._append = this._searchParams.append;
          this._searchParams.append = function (name, value) {
            this._append(name, value);
            this._url.search = this.toString();
          };
          this._searchParams._delete = this._searchParams.delete;
          this._searchParams.delete = function (name) {
            this._delete(name);
            this._url.search = this.toString();
          };
          this._searchParams._set = this._searchParams.set;
          this._searchParams.set = function (name, value) {
            this._set(name, value);
            this._url.search = this.toString();
          };
          this._searchParams._sort = this._searchParams.sort;
          this._searchParams.sort = function () {
            this._sort();
            this._url.search = this.toString();
          };
        }
        return this._searchParams;
      },
    });
  }
})();
