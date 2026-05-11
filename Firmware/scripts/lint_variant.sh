#!/bin/bash
# Checks variant .conf files for duplicate Kconfig choice assignments.
# Usage: lint_variant.sh variants/*.conf

rc=0
for conf in "$@"; do
    # Extract choice prefixes (e.g. CONFIG_CELL_1_TYPE, CONFIG_POWER_MODE)
    # and flag any prefix that appears more than once with =y
    dupes=$(grep -oP 'CONFIG_(?:CELL_\d_TYPE|POWER_MODE|BATTERY_CHEMISTRY|PPO2_CONTROL_DEFAULT|CAL_MODE_DEFAULT)_\w+(?==y)' "$conf" \
        | sed 's/_[^_]*$//' \
        | sort | uniq -d)

    if [ -n "$dupes" ]; then
        echo "ERROR: $conf has duplicate choice assignments:"
        for prefix in $dupes; do
            grep "${prefix}_" "$conf"
        done
        rc=1
    fi
done

if [ $rc -eq 0 ]; then
    echo "All variant configs clean."
fi
exit $rc
