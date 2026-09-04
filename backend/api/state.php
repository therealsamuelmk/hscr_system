<?php
// api/state.php — polled by a watch.
// GET ?table=1  ->  { ok, table, state, service }
// The watch decides how to render `service`; this endpoint just reports it.
declare(strict_types=1);
require __DIR__ . '/_bootstrap.php';

if ($_SERVER['REQUEST_METHOD'] !== 'GET') {
    json_fail(405, 'GET required');
}

$table = filter_var($_GET['table'] ?? null, FILTER_VALIDATE_INT);
if ($table === false || $table === null || !in_array($table, ACTIVE_TABLES, true)) {
    json_fail(400, 'invalid table');
}

$stmt = db()->prepare('SELECT state, service_type FROM hscr_tables WHERE table_number = ?');
$stmt->bind_param('i', $table);
$stmt->execute();
$row = $stmt->get_result()->fetch_assoc();

if (!$row) {
    json_fail(404, 'unknown table');
}

json_ok([
    'table' => $table,
    'state' => (int) $row['state'],
    'service' => $row['service_type'],
]);
