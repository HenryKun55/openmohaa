#!/bin/bash
# Quick puller for the Vita boot.log + crashlog. Usage: ./fetch_log.sh [IP]
IP=${1:-192.168.1.6}
OUT=/tmp/omha_logs
mkdir -p "$OUT"

echo "Pulling from $IP..."
for f in boot.log crashlog.txt; do
    curl --disable-epsv -sS --connect-timeout 10 \
        "ftp://$IP:1337/ux0:/data/openmohaa/main/$f" \
        -o "$OUT/$f" \
        -w "  $f: HTTP %{http_code} %{size_download}B\n"
done

echo ""
echo "=== Last CROSSHAIR / GPU / LoadTGA lines ==="
grep -E "CROSSHAIR:|^GPU:|LoadTGA:" "$OUT/boot.log" | tail -20
echo ""
echo "Files at: $OUT/"
