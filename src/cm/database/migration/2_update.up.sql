BEGIN TRANSACTION;

DROP TABLE IF EXISTS imagemanager;

CREATE TABLE IF NOT EXISTS imagemanager_items (
    itemID TEXT,
    type TEXT,
    version TEXT,
    preinstalled INTEGER,
    state TEXT,
    errorAosCode INTEGER,
    errorSysCode INTEGER,
    errorMessage TEXT,
    rootDigest TEXT,
    timestamp INTEGER,
    PRIMARY KEY (itemID, version)
);

CREATE TABLE IF NOT EXISTS imagemanager_blobs (
    itemID TEXT,
    version TEXT,
    digest TEXT,
    mediaType TEXT,
    size INTEGER,
    isLayer INTEGER,
    diffID TEXT,
    PRIMARY KEY (itemID, version, digest)
);

CREATE INDEX IF NOT EXISTS imagemanager_blobs_digest ON imagemanager_blobs(digest);

ALTER TABLE launcher_instances ADD COLUMN indexDigest TEXT NOT NULL DEFAULT '';
ALTER TABLE launcher_run_requests ADD COLUMN indexDigest TEXT NOT NULL DEFAULT '';

UPDATE SchemaVersion SET version = 2;

COMMIT;
