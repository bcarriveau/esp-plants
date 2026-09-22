// alpha.13 intentionally leaves no runtime LVGL button wrapper here.
//
// alpha.12 attempted to override LV_STYLE_TRANSITION by attaching a local
// style whose transition descriptor was nullptr. That could be dereferenced
// by LVGL's style/transition machinery during UI construction and caused an
// immediate LoadProhibited boot loop.
//
// Default-theme transitions are now disabled safely at compile time with
// LV_THEME_DEFAULT_TRANSITION_TIME=0 in include/lv_conf.h.
