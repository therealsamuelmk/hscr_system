<?php
// api/resolve.php — called by a watch when staff presses the "done" button.
// POST { "table": 1 }  ->  clears that table's request back to idle.
declare(strict_types=1);
require __DIR__ . '/_bootstrap.php';

if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    json_fail(405, 'POST required');
}

$body = read_json_body();
$table = filter_var($body['table'] ?? null, FILTER_VALIDATE_INT);

if ($table === false || $table === null || !in_array($table, ACTIVE_TABLES, true)) {
    json_fail(400, 'invalid table');
}

$stmt = db()->prepare(
    'UPDATE hscr_tables SET state = 0, service_type = NULL, resolved_at = NOW() WHERE table_number = ?'
);
$stmt->bind_param('i', $table);
$stmt->execute();

json_ok(['table' => $table]);
