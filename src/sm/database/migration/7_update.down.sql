BEGIN TRANSACTION;

DROP TABLE IF EXISTS imagemanager_blobs;
DROP TABLE IF EXISTS imagemanager_items;

CREATE TABLE IF NOT EXISTS items (
    itemID TEXT,
    type TEXT,
    version TEXT,
    manifestDigest TEXT,
    state TEXT,
    timestamp INTEGER,
    PRIMARY KEY(itemID, type, version)
);

COMMIT;
