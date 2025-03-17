var __swc_crazy_long_line_swc_inherit_polyfill = {
    _: () => {}
}
var _Users_ammarahmed_Downloads_ns_vue_rspack_nativescript_rspack_dist_helpers_swc_call_super_polyfill_js__WEBPACK_IMPORTED_MODULE_0__ = {
    _: () => {}
}

var _swc_helpers_possible_constructor_return__WEBPACK_IMPORTED_MODULE_4__ = {
    _: () => {}
}

var _swc_helpers_class_call_check__WEBPACK_IMPORTED_MODULE_2__ = {
    _: () => {}
}

var _swc_helpers_create_class__WEBPACK_IMPORTED_MODULE_5__ = {
    _: () => {}
}

var _swc_helpers_ts_decorate__WEBPACK_IMPORTED_MODULE_6__ = {
    __decorate: () => {}
}

var ClickListener = /*#__PURE__*/ function(_java_lang_Object) {
    "use strict";
    (0, __swc_crazy_long_line_swc_inherit_polyfill._)(ClickListener, _java_lang_Object);
    function ClickListener() {
        (0,_swc_helpers_class_call_check__WEBPACK_IMPORTED_MODULE_2__._)(this, ClickListener);
        var _this;
        _this = (0,_Users_ammarahmed_Downloads_ns_vue_rspack_nativescript_rspack_dist_helpers_swc_call_super_polyfill_js__WEBPACK_IMPORTED_MODULE_0__._)(this, 
            ClickListener);
        // necessary when extending TypeScript constructors
        return (0,_swc_helpers_possible_constructor_return__WEBPACK_IMPORTED_MODULE_4__._)(_this, global.__native(_this));
    }
    (0,_swc_helpers_create_class__WEBPACK_IMPORTED_MODULE_5__._)(ClickListener, [
        {
            key: "onClick",
            value: function onClick(view) {
                console.log("Button clicked!");
            }
        },
        {
            key: "onDone",
            value: function onClick(view) {
                console.log("Button clicked!");
            }
        }
    ]);
    return ClickListener;
}(java.lang.Object);

ClickListener = (0,_swc_helpers_ts_decorate__WEBPACK_IMPORTED_MODULE_6__.__decorate)([
    NativeClass,
    Interfaces([
        android.view.View.OnClickListener
    ]),
    (0,_swc_helpers_ts_decorate__WEBPACK_IMPORTED_MODULE_6__.__metadata)("design:type", Function),
    (0,_swc_helpers_ts_decorate__WEBPACK_IMPORTED_MODULE_6__.__metadata)("design:paramtypes", [])
], ClickListener);
var myObj = new MyObject("world");
