#!/bin/bash

usage() {
    echo "Usage: $0 [-f ips.txt] [ip_address1 ip_address2 ...]"
    echo "  -f ips.txt   Specify a file containing IP addresses (one per line)."
    echo "  ip_address   Provide IP addresses as arguments."
    exit 1
}

IPS=()

while getopts ":f:" opt; do
    case $opt in
        f)
            if [ -f "$OPTARG" ]; then
                while IFS= read -r line; do
                    [[ "$line" =~ ^#.*$ ]] && continue
                    [[ -z "$line" ]] && continue
                    IPS+=("$line")
                done < "$OPTARG"
            else
                echo "Error: File $OPTARG not found."
                exit 1
            fi
            ;;
        \?)
            echo "Invalid option: -$OPTARG" >&2
            usage
            ;;
        :)
            echo "Option -$OPTARG requires an argument." >&2
            usage
            ;;
    esac
done

shift $((OPTIND -1))

if [ $# -gt 0 ]; then
    IPS+=("$@")
fi

if [ ${#IPS[@]} -eq 0 ]; then
    echo "Error: No IP addresses provided."
    usage
fi

SRC_DIR=~/CLionProjects/io_uring_fast_net_new

USER=ubuntu

EXCLUDES=(
    '--exclude' '.git'
    '--exclude' 'build'
    '--exclude' 'cmake-build-debug'
    '--exclude' 'cmake-build-release'
    '--exclude' 'other'
    '--exclude' 'results'
    '--exclude' 'results*/'
    '--exclude' 'reports'
    '--exclude' 'reports*/'
    '--exclude' 'venv'
    '--exclude' 'dpdk_demo'
    '--exclude' 'reports_c7gn16x_2024SEP18'
)

SSH_OPTIONS="-i ~/.ssh/tqdm -o StrictHostKeyChecking=no"

for IP in "${IPS[@]}"; do
    echo "Syncing to $USER@$IP:~/"
    rsync -av --progress -e "ssh $SSH_OPTIONS" "${EXCLUDES[@]}" "$SRC_DIR" "$USER@$IP:~/" &
done
wait
