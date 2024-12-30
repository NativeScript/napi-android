#!/system/bin/sh
#!/system/bin/sh
HERE=$(cd "$(dirname "$0")" && pwd)

cmd=$1
shift

# This must be called *before* `LD_PRELOAD` is set. Otherwise, if this is a 32-
# bit app running on a 64-bit device, the 64-bit getprop will fail to load
# because it will preload a 32-bit ASan runtime.
# https://github.com/android/ndk/issues/1744
os_version=$(getprop ro.build.version.sdk)

if [ "$os_version" -eq "27" ]; then
  cmd="$cmd -Xrunjdwp:transport=dt_android_adb,suspend=n,server=y -Xcompiler-option --debuggable $@"
elif [ "$os_version" -eq "28" ]; then
  cmd="$cmd -XjdwpProvider:adbconnection -XjdwpOptions:suspend=n,server=y -Xcompiler-option --debuggable $@"
else
  cmd="$cmd -XjdwpProvider:adbconnection -XjdwpOptions:suspend=n,server=y $@"
fi

LD_HWASAN=1 exec $cmd
