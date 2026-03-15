/* ── FieldTunnel Web UI v0.3.3 ── */

var cfgLoaded = false;
var dirty = false;

document.addEventListener('change', function(e) {
    if (e.target.matches('input, select')) dirty = true;
});

/* ── Navigation ── */
document.querySelectorAll('.sb-nav a').forEach(function(a) {
    a.addEventListener('click', function(e) {
        e.preventDefault();
        document.querySelectorAll('.sb-nav a').forEach(function(x) { x.classList.remove('active'); });
        document.querySelectorAll('.page').forEach(function(x) { x.classList.remove('active'); });
        a.classList.add('active');
        var p = document.getElementById('page-' + a.dataset.page);
        if (p) p.classList.add('active');
    });
});

/* ── Status polling ── */
function poll() {
    fetch('/api/status').then(function(r) { return r.json(); }).then(function(d) {
        /* Sidebar */
        var dot = document.getElementById('pulse');
        var st = document.getElementById('sb-status');
        var ip = document.getElementById('sb-ip');
        dot.classList.add('on');
        if (d.wifiConnected) {
            st.textContent = 'Connected';
            ip.textContent = d.staIp || d.apIp;
        } else {
            st.textContent = 'AP Only';
            ip.textContent = d.apIp;
        }

        /* Dashboard — Device */
        document.getElementById('d-devid').textContent = d.deviceId || '--';
        document.getElementById('d-host').textContent = (d.hostname || '--') + '.local';
        document.getElementById('d-apssid').textContent = d.apSsid || '--';

        /* Dashboard — Connection */
        document.getElementById('d-mode').textContent = d.wifiConnected ? 'APSTA' : 'AP';
        document.getElementById('d-apip').textContent = d.apIp || '--';
        document.getElementById('d-staip').textContent = d.wifiConnected ? d.staIp : 'Not connected';
        document.getElementById('d-nat').textContent = d.natEnabled ? 'Active' : 'Inactive';
        document.getElementById('d-port').textContent = d.port;
        document.getElementById('d-proto').textContent = MODE_NAMES[d.mode] || 'Unknown';
        document.getElementById('d-frame').textContent =
            d.baud + ' ' + d.dataBits + ['N','O','E'][d.parity] + d.stopBits;
        document.getElementById('d-err').textContent = d.lastError || 'None';
        document.getElementById('d-tx').textContent = d.tx;
        document.getElementById('d-rx').textContent = d.rx;
        document.getElementById('d-errs').textContent = d.err;
        document.getElementById('d-up').textContent = fmtUp(d.uptime);

        /* About */
        document.getElementById('a-fw').textContent = 'v' + d.fw;
        document.getElementById('a-mac').textContent = d.mac;

        /* Firmware page */
        document.getElementById('f-devid').textContent = d.deviceId;
        document.getElementById('f-mac').textContent = d.mac;
        document.getElementById('f-host').textContent = (d.hostname || '') + '.local';
        document.getElementById('f-fw').textContent = 'v' + d.fw;
    }).catch(function() {
        document.getElementById('pulse').classList.remove('on');
        document.getElementById('sb-status').textContent = 'Offline';
    });
}

function loadConfig() {
    fetch('/api/config').then(function(r) { return r.json(); }).then(function(d) {
        document.getElementById('wifi-ssid').value = d.ssid || '';
        document.getElementById('cfg-baud').value = d.baud;
        document.getElementById('cfg-dbits').value = d.dataBits;
        document.getElementById('cfg-par').value = d.parity;
        document.getElementById('cfg-stop').value = d.stopBits;
        document.getElementById('cfg-tmo').value = d.rtuTimeout;
        document.getElementById('cfg-port').value = d.tcpPort;
        setActiveMode(d.mode);
        updateModeStatus(d.mode, d.tcpPort, d.baud,
            d.dataBits, d.parity, d.stopBits);
        cfgLoaded = true;
    }).catch(function() {});
}

function fmtUp(s) {
    if (s < 0) s = 0;
    var d = Math.floor(s / 86400);
    var h = Math.floor((s % 86400) / 3600);
    var m = Math.floor((s % 3600) / 60);
    var sec = s % 60;
    var parts = [];
    if (d) parts.push(d + 'd');
    if (h) parts.push(h + 'h');
    if (m) parts.push(m + 'm');
    parts.push(sec + 's');
    return parts.join(' ');
}

/* ── Mode cards ── */
var MODE_NAMES = ['Modbus TCP Gateway', 'Raw TCP Tunnel', 'BACnet MS/TP', 'SunSpec Solar'];
var currentMode = 0;

function setActiveMode(m) {
    currentMode = m;
    document.querySelectorAll('.mode-card').forEach(function(c) {
        c.classList.toggle('active', parseInt(c.dataset.mode) === m);
    });
    var bs = document.getElementById('bacnet-settings');
    var se = document.getElementById('serial-extras');
    var bh = document.getElementById('baud-hint');
    var me = document.getElementById('modbus-extras');
    if (m === 2) {
        if (se) se.style.display = 'none';
        if (me) me.style.display = 'none';
        if (bs) bs.style.display = 'block';
        if (bh) bh.style.display = 'block';
    } else {
        if (se) se.style.display = 'block';
        if (me) me.style.display = (m === 0) ? 'flex' : 'none';
        if (bs) bs.style.display = 'none';
        if (bh) bh.style.display = 'none';
    }
}

function updateModeStatus(m, port, baud, db, par, sb) {
    var el = document.getElementById('mode-status');
    if (!el) return;
    if (m === 2) {
        var bmac = document.getElementById('cfg-bmac');
        var bport = document.getElementById('cfg-bport');
        el.textContent = 'Active: BACnet MS/TP \u00b7 MAC ' +
            (bmac ? bmac.value : '5') + ' \u00b7 ' + baud +
            ' \u00b7 UDP ' + (bport ? bport.value : '47808');
    } else {
        var frame = baud + ' ' + db + ['N','O','E'][par] + sb;
        el.textContent = 'Active: ' + MODE_NAMES[m] + ' \u00b7 port ' + port + ' \u00b7 ' + frame;
    }
}

function selectMode(m) {
    if (m === currentMode) return;
    if (dirty && !confirm('You have unsaved changes. Switch mode anyway?')) return;
    if (!confirm('Switch to ' + MODE_NAMES[m] + '?\n\nActive connections will be dropped.\nDevice will apply new mode immediately.')) return;
    dirty = false;
    setActiveMode(m);
    var body = {
        baud: parseInt(document.getElementById('cfg-baud').value),
        dataBits: parseInt(document.getElementById('cfg-dbits').value),
        parity: parseInt(document.getElementById('cfg-par').value),
        stopBits: parseInt(document.getElementById('cfg-stop').value),
        rtuTimeout: parseInt(document.getElementById('cfg-tmo').value),
        tcpPort: parseInt(document.getElementById('cfg-port').value),
        mode: m
    };
    fetch('/api/rs485', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(body)
    }).then(function(r) { return r.json(); }).then(function(d) {
        if (d.ok) {
            updateModeStatus(m, body.tcpPort, body.baud,
                body.dataBits, body.parity, body.stopBits);
            poll();
        }
    }).catch(function(e) { alert('Error: ' + e); setActiveMode(currentMode); });
}

/* ── BACnet config ── */
function loadBACnet() {
    fetch('/api/bacnet').then(function(r) { return r.json(); }).then(function(d) {
        document.getElementById('cfg-bmac').value = d.mac;
        document.getElementById('cfg-bmax').value = d.maxMaster;
        document.getElementById('cfg-bport').value = d.port;
        document.getElementById('cfg-bdev').value = d.deviceId;
        document.getElementById('cfg-bnet').value = d.network;
        document.getElementById('cfg-bslot').value = d.slotTime;
        document.getElementById('cfg-breply').value = d.replyTimeout;
        document.getElementById('cfg-busage').value = d.usageTimeout;
        document.getElementById('cfg-bretry').value = d.retry;
        document.getElementById('cfg-bdname').value = d.deviceName || '';
    }).catch(function() {});
}

function saveBACnet() {
    var body = {
        mac: parseInt(document.getElementById('cfg-bmac').value),
        maxMaster: parseInt(document.getElementById('cfg-bmax').value),
        port: parseInt(document.getElementById('cfg-bport').value),
        deviceId: parseInt(document.getElementById('cfg-bdev').value),
        network: parseInt(document.getElementById('cfg-bnet').value),
        slotTime: parseInt(document.getElementById('cfg-bslot').value),
        replyTimeout: parseInt(document.getElementById('cfg-breply').value),
        usageTimeout: parseInt(document.getElementById('cfg-busage').value),
        retry: parseInt(document.getElementById('cfg-bretry').value),
        deviceName: document.getElementById('cfg-bdname').value
    };
    fetch('/api/bacnet', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(body)
    }).then(function(r) { return r.json(); }).then(function(d) {
        if (d.ok) { dirty = false; alert('BACnet settings applied.'); }
    }).catch(function(e) { alert('Error: ' + e); });
}

/* ── RS485 config ── */
function saveRS485() {
    var body = {
        baud: parseInt(document.getElementById('cfg-baud').value),
        dataBits: parseInt(document.getElementById('cfg-dbits').value),
        parity: parseInt(document.getElementById('cfg-par').value),
        stopBits: parseInt(document.getElementById('cfg-stop').value),
        rtuTimeout: parseInt(document.getElementById('cfg-tmo').value),
        tcpPort: parseInt(document.getElementById('cfg-port').value),
        mode: currentMode
    };
    fetch('/api/rs485', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(body)
    }).then(function(r) { return r.json(); }).then(function(d) {
        if (d.ok) {
            updateModeStatus(currentMode, body.tcpPort, body.baud,
                body.dataBits, body.parity, body.stopBits);
            dirty = false;
            alert('RS485 settings applied.');
        }
    }).catch(function(e) { alert('Error: ' + e); });
}

/* ── WiFi scan ── */
function rssiIcon(rssi) {
    if (rssi >= -50) return '\u2588\u2588\u2588\u2588';
    if (rssi >= -70) return '\u2588\u2588\u2588\u2591';
    if (rssi >= -80) return '\u2588\u2588\u2591\u2591';
    return '\u2588\u2591\u2591\u2591';
}

function scanNetworks() {
    var sel = document.getElementById('wifi-scan');
    var btn = document.querySelector('.btn-scan');
    sel.disabled = true;
    sel.innerHTML = '<option value="">Scanning\u2026</option>';
    btn.disabled = true;
    btn.textContent = 'Scanning\u2026';

    fetch('/api/scan').then(function(r) { return r.json(); }).then(function(d) {
        sel.innerHTML = '';
        if (!d.networks || d.networks.length === 0) {
            sel.innerHTML = '<option value="">No networks found</option>';
            sel.disabled = true;
        } else {
            sel.innerHTML = '<option value="">-- select network --</option>';
            d.networks.forEach(function(net) {
                var o = document.createElement('option');
                o.value = net.ssid;
                o.textContent = net.ssid + '  ' + rssiIcon(net.rssi) + '  ' + net.rssi + ' dBm' + (net.auth > 0 ? '  \uD83D\uDD12' : '');
                sel.appendChild(o);
            });
            sel.disabled = false;
        }
    }).catch(function() {
        sel.innerHTML = '<option value="">Scan failed</option>';
        sel.disabled = true;
    }).finally(function() {
        btn.disabled = false;
        btn.textContent = 'Scan';
    });
}

document.getElementById('wifi-scan').addEventListener('change', function() {
    var v = this.value;
    if (v) document.getElementById('wifi-ssid').value = v;
});

/* ── WiFi save ── */
function saveWifi() {
    var body = {
        ssid: document.getElementById('wifi-ssid').value,
        pass: document.getElementById('wifi-pass').value
    };
    if (!body.ssid) { alert('SSID is required.'); return; }
    if (!confirm('Device will reboot to apply WiFi settings. Continue?')) return;
    fetch('/api/wifi', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(body)
    }).then(function() {
        alert('WiFi saved. Device is rebooting...');
    }).catch(function() {
        alert('WiFi saved. Device is rebooting...');
    });
}

/* ── OTA Firmware upload ── */
function uploadFirmware() {
    var fileInput = document.getElementById('ota-file');
    var file = fileInput.files[0];
    if (!file) { alert('Select a .bin file first.'); return; }
    if (!file.name.endsWith('.bin')) { alert('File must be a .bin firmware image.'); return; }
    if (!confirm('Upload ' + file.name + ' (' + (file.size / 1024).toFixed(0) + ' KB)?\nDevice will reboot after update.')) return;

    var bar = document.getElementById('ota-bar');
    var progress = document.getElementById('ota-progress');
    var status = document.getElementById('ota-status');

    progress.classList.add('active');
    bar.style.width = '0%';
    status.textContent = 'Uploading\u2026 0%';

    var xhr = new XMLHttpRequest();

    xhr.upload.onprogress = function(e) {
        if (e.lengthComputable) {
            var pct = Math.round(e.loaded / e.total * 100);
            bar.style.width = pct + '%';
            status.textContent = 'Uploading\u2026 ' + pct + '%';
        }
    };

    xhr.onload = function() {
        try {
            var d = JSON.parse(xhr.responseText);
            if (d.status === 'rebooting') {
                bar.style.width = '100%';
                status.textContent = 'Verifying firmware\u2026';
                setTimeout(function() {
                    status.textContent = 'Rebooting device\u2026';
                    otaCountdown(12);
                }, 1000);
            } else if (d.error) {
                status.textContent = 'Error: ' + d.error;
                bar.classList.add('error');
            }
        } catch(e) {
            status.textContent = 'Error: ' + xhr.responseText;
        }
    };

    xhr.onerror = function() {
        status.textContent = 'Upload failed \u2014 connection lost';
    };

    xhr.open('POST', '/api/ota/upload');
    xhr.setRequestHeader('Content-Type', 'application/octet-stream');
    xhr.send(file);
}

function otaCountdown(sec) {
    var status = document.getElementById('ota-status');
    if (sec <= 0) {
        status.textContent = 'Reloading\u2026';
        window.location.reload();
        return;
    }
    status.textContent = 'Reconnecting in ' + sec + 's\u2026';
    setTimeout(function() { otaCountdown(sec - 1); }, 1000);
}

function checkOTA() {
    var el = document.getElementById('ota-check');
    el.textContent = 'Checking\u2026';
    el.style.color = '';
    fetch('/api/ota/check').then(function(r) { return r.json(); }).then(function(d) {
        if (d.checkFailed) {
            el.textContent = 'Check failed \u2014 no internet access';
            el.style.color = 'var(--red)';
            return;
        }
        if (d.updateAvailable) {
            el.innerHTML =
                '<div class="update-banner">' +
                '<strong>Update available: v' + d.available + '</strong>' +
                '<div style="font-size:.75rem;color:var(--muted);margin:6px 0">' + d.notes + '</div>' +
                '<div style="display:flex;gap:8px;margin-top:10px">' +
                '<button onclick="otaFetchUpdate(\'' + d.url + '\')">Update Now</button>' +
                '<button class="btn-ghost" onclick="dismissUpdate()">Later</button>' +
                '</div></div>';
        } else {
            el.textContent = 'Up to date \u2014 v' + d.current;
            el.style.color = 'var(--teal)';
        }
    }).catch(function(e) {
        el.textContent = 'Check failed: ' + e;
        el.style.color = 'var(--red)';
    });
}

function otaFetchUpdate(url) {
    if (!confirm('Device will reboot after update.\nRS485 bridge will be offline for ~30 seconds.\nContinue?')) return;

    var bar = document.getElementById('ota-bar');
    var progress = document.getElementById('ota-progress');
    var status = document.getElementById('ota-status');

    progress.classList.add('active');
    bar.style.width = '30%';
    status.textContent = 'Downloading firmware from server\u2026';

    fetch('/api/ota/fetch', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({url: url})
    }).then(function(r) { return r.json(); }).then(function(d) {
        if (d.status === 'rebooting') {
            bar.style.width = '100%';
            status.textContent = 'Firmware installed \u2014 rebooting\u2026';
            otaCountdown(15);
        } else if (d.error) {
            status.textContent = 'Update failed: ' + d.error;
            bar.classList.add('error');
        }
    }).catch(function() {
        bar.style.width = '100%';
        status.textContent = 'Device is rebooting\u2026';
        otaCountdown(15);
    });
}

function dismissUpdate() {
    var el = document.getElementById('ota-check');
    el.textContent = 'Reminder set \u2014 will check again on next page load';
    el.style.color = 'var(--muted)';
}

/* ── Test console ── */
function sendTest() {
    var out = document.getElementById('t-out');
    out.textContent = 'Sending...';
    out.className = 'console';

    var body = {
        slaveId: parseInt(document.getElementById('t-slave').value),
        fc: parseInt(document.getElementById('t-fc').value),
        addr: parseInt(document.getElementById('t-addr').value),
        count: parseInt(document.getElementById('t-count').value)
    };

    fetch('/api/test', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(body)
    }).then(function(r) { return r.json(); }).then(function(d) {
        var lines = '';
        lines += 'TX >> ' + d.txHex + '\n';
        if (d.ok) {
            lines += 'RX << ' + d.rxHex + '\n';
            if (d.values && d.values.length > 0) {
                lines += '\n';
                var fc = body.fc;
                if (fc === 1 || fc === 2) {
                    lines += 'Coils (addr ' + d.startAddr + '):\n';
                    for (var i = 0; i < d.values.length; i++) {
                        lines += '  [' + (d.startAddr + i) + '] = ' + (d.values[i] ? 'ON' : 'OFF') + '\n';
                    }
                } else if (fc === 3 || fc === 4) {
                    lines += 'Registers (addr ' + d.startAddr + '):\n';
                    for (var i = 0; i < d.values.length; i++) {
                        lines += '  [' + (d.startAddr + i) + '] = ' +
                            d.values[i] + ' (0x' + d.values[i].toString(16).toUpperCase().padStart(4,'0') + ')\n';
                    }
                } else if (fc === 5) {
                    lines += 'Coil written: ' + (d.values[0] === 0xFF00 ? 'ON' : 'OFF') + '\n';
                } else if (fc === 6) {
                    lines += 'Register written: ' + d.values[0] + '\n';
                }
            }
        } else {
            lines += 'ERROR: ' + (d.error || 'No response') + '\n';
        }
        out.textContent = lines;
    }).catch(function(e) {
        out.textContent = 'Fetch error: ' + e;
    });
}

/* ── Actions ── */
function resetStats() {
    fetch('/api/resetstats', { method: 'POST' })
        .then(function() { poll(); })
        .catch(function(e) { alert('Error: ' + e); });
}

function reboot() {
    if (!confirm('Reboot the device?')) return;
    fetch('/api/reboot', { method: 'POST' }).catch(function() {});
    alert('Device is rebooting...');
}

/* ── Init ── */
poll();
loadConfig();
loadBACnet();
setInterval(poll, 2000);
