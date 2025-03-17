"use strict";
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

var _swc_helpers_define_property__WEBPACK_IMPORTED_MODULE_9__ = {
    _: () => {}
}
var _swc_helpers_get__WEBPACK_IMPORTED_MODULE_7__ =  {
    _: () => {}
}

var _swc_helpers_get_prototype_of__WEBPACK_IMPORTED_MODULE_8__ = {
    _: () => {}
}

var _android_app_Activity;
var TestActivity = /*#__PURE__*/ function(_superClass) {
    "use strict";
    (0,__swc_crazy_long_line_swc_inherit_polyfill._)(TestActivity, _superClass);
    function TestActivity() {
        (0,_swc_helpers_class_call_check__WEBPACK_IMPORTED_MODULE_2__._)(this, TestActivity);
        return (0,_Users_ammarahmed_Downloads_ns_vue_rspack_nativescript_rspack_dist_helpers_swc_call_super_polyfill_js__WEBPACK_IMPORTED_MODULE_0__._)(this, TestActivity, arguments);
    }
    (0,_swc_helpers_create_class__WEBPACK_IMPORTED_MODULE_5__._)(TestActivity, [
        {
            key: "onCreate",
            value: function onCreate(savedInstanceState) {
                (0,_swc_helpers_get__WEBPACK_IMPORTED_MODULE_7__._)((0,_swc_helpers_get_prototype_of__WEBPACK_IMPORTED_MODULE_8__._)(TestActivity.prototype), "onCreate", this).call(this, savedInstanceState);
                console.log(TestActivity.TEST1);
            }
        }
    ]);
    return TestActivity;
}(_android_app_Activity = android.app.Activity);
(0,_swc_helpers_define_property__WEBPACK_IMPORTED_MODULE_9__._)(TestActivity, "TEST1", "my_test");
TestActivity = (0,_swc_helpers_ts_decorate__WEBPACK_IMPORTED_MODULE_6__.__decorate)([
    JavaProxy('org.nativescript.MyCustomActivity')
], TestActivity);