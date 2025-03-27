mkdir -p /run/udev/rules.d
cat > /run/udev/rules.d/mutter-kvm-tests.rules << __EOF__
KERNELS=="*input*", ATTRS{name}=="Test *", ENV{ID_SEAT}="meta-test-seat0"
__EOF__

udevadm control --reload-rules
