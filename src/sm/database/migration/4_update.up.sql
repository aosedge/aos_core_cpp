BEGIN TRANSACTION;

ALTER TABLE instances DROP COLUMN networkParameters;

ALTER TABLE instancenetwork ADD COLUMN networkConfig TEXT;
ALTER TABLE instancenetwork ADD COLUMN allocatedParams TEXT;

COMMIT;
