qemu-system-x86_64 -kernel build/sync-test_kvm-x86_64 -nographic -m 512 -smp cores=6,sockets=1,threads=1 -cpu host -enable-kvm
