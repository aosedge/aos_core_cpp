#!/bin/sh

COMMAND="$1"

clear_disks() {
    echo "Remove IAM DB and PKCS11 storage"

    rm /var/aos/iam -rf

    # Clear SoftHSM storage
    rm /var/lib/softhsm/tokens/* -rf

    sync
}

remove_firewall_rules() {
    nft delete table inet aos-provfw 2>/dev/null
}

deprovision_async() {
    {
        sleep 1

        # use systemctl stop all aos.target services instead of systemctl stop aos.target, because this approach doesn't wait
        # all services really stopped.
        systemctl stop -- $(systemctl show -p Wants aos.target | cut -d= -f2)

        remove_firewall_rules

        echo "Restore unprovisioned flag" | systemd-cat
        rm /var/aos/.provisionstate -f

        systemctl start aos.target
    } > /dev/null 2>&1 &
}

case "$COMMAND" in
async)
    deprovision_async
    ;;

*)
    clear_disks
    ;;
esac
