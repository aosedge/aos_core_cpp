BEGIN TRANSACTION;

ALTER TABLE instances ADD COLUMN networkParameters TEXT;

ALTER TABLE instancenetwork DROP COLUMN networkConfig;
ALTER TABLE instancenetwork DROP COLUMN allocatedParams;

COMMIT;
