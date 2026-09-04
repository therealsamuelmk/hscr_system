<?php
// api/request.php — called by the guest portal.
// POST { "table": 1, "service": "waiter" }  ->  marks that table's request active.
declare(strict_types=1);
require __DIR__ . '/_bootstrap.php';

if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    json_fail(405, 'POST required');
}

$body = read_json_body();
$table = filter_var($body['table'] ?? null, FILTER_VALIDATE_INT);
$service = $body['service'] ?? null;

if ($table === false || $table === null || !in_array($table, ACTIVE_TABLES, true)) {
    json_fail(400, 'invalid table');
}
if (!is_string($service) || !in_array($service, SERVICE_TYPES, true)) {
    json_fail(400, 'invalid service');
}

$stmt = db()->prepare(
    'UPDATE hscr_tables SET state = 1, service_type = ?, requested_at = NOW() WHERE table_number = ?'
);
$stmt->bind_param('si', $service, $table);
$stmt->execute();

json_ok(['table' => $table, 'service' => $service]);
