BEGIN TRANSACTION;

DROP TABLE IF EXISTS imagemanager_blobs;
DROP TABLE IF EXISTS imagemanager_items;

CREATE TABLE IF NOT EXISTS imagemanager (
    itemID TEXT,
    type TEXT,
    version TEXT,
    indexDigest TEXT,
    state TEXT,
    timestamp INTEGER,
    PRIMARY KEY (itemID, version)
);

COMMIT;
