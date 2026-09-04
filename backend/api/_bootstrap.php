<?php
// _bootstrap.php — shared DB connection + JSON helpers for the HSCR API.
// Included by request.php / state.php / resolve.php. Not routable itself
// (the leading underscore is just convention; access is harmless but this
// file has no behaviour of its own).

declare(strict_types=1);

header('Content-Type: application/json; charset=utf-8');

// Tables that actually have a watch assigned, for this phase.
const ACTIVE_TABLES = [1, 2];
// Must match the SERVICES keys in hscr-portal.html.
const SERVICE_TYPES = ['payment', 'waiter', 'order'];

function json_fail(int $httpStatus, string $message): void {
    http_response_code($httpStatus);
    echo json_encode(['ok' => false, 'error' => $message]);
    exit;
}

function json_ok(array $data = []): void {
    echo json_encode(['ok' => true] + $data);
    exit;
}

function read_json_body(): array {
    $raw = file_get_contents('php://input');
    $data = json_decode($raw, true);
    return is_array($data) ? $data : [];
}

function db(): mysqli {
    static $conn = null;
    if ($conn !== null) return $conn;

    // hscr-db.local.php (gitignored, XAMPP-only) overrides hscr-db.php when
    // present, so the same API code runs against the local dev DB without
    // ever touching the live server's credentials. On the live server only
    // hscr-db.php exists, so this falls through to it unchanged.
    $base = dirname(__DIR__, 2);
    $configPath = file_exists("$base/hscr-db.local.php") ? "$base/hscr-db.local.php" : "$base/hscr-db.php";

    $cfg = require $configPath;
    $conn = mysqli_init();
    if (!$conn->real_connect($cfg['host'], $cfg['user'], $cfg['pass'], $cfg['name'])) {
        json_fail(500, 'database connection failed');
    }
    return $conn;
}
