BEGIN TRANSACTION;

-- Instance ident contains version instead of preinstalled flag. The version of the existing instances is taken from
-- the latest launcher instance with the same ident.

CREATE TABLE storagestate_new (
    itemID TEXT,
    subjectID TEXT,
    instance INTEGER,
    type TEXT,
    version TEXT NOT NULL DEFAULT '',
    storageQuota INTEGER,
    stateQuota INTEGER,
    stateChecksum BLOB,
    PRIMARY KEY(itemID, subjectID, instance, type, version)
);

INSERT OR REPLACE INTO storagestate_new (itemID, subjectID, instance, type, version, storageQuota, stateQuota,
    stateChecksum)
SELECT s.itemID, s.subjectID, s.instance, s.type,
    COALESCE((SELECT l.version FROM launcher_instances l WHERE l.itemID = s.itemID AND l.subjectID = s.subjectID
        AND l.instance = s.instance AND l.type = s.type ORDER BY l.timestamp DESC LIMIT 1), ''),
    s.storageQuota, s.stateQuota, s.stateChecksum FROM storagestate s;

DROP TABLE storagestate;
ALTER TABLE storagestate_new RENAME TO storagestate;

CREATE TABLE networkmanager_instances_new (
    itemID TEXT,
    subjectID TEXT,
    instance INTEGER,
    type TEXT,
    version TEXT NOT NULL DEFAULT '',
    networkID TEXT,
    nodeID TEXT,
    ip TEXT,
    exposedPorts TEXT,
    dnsServers TEXT,
    hostnames TEXT NOT NULL DEFAULT '[]',
    PRIMARY KEY(itemID, subjectID, instance, type, version),
    FOREIGN KEY(networkID) REFERENCES networks(networkID),
    FOREIGN KEY(networkID, nodeID) REFERENCES hosts(networkID, nodeID)
);

INSERT OR REPLACE INTO networkmanager_instances_new (itemID, subjectID, instance, type, version, networkID, nodeID,
    ip, exposedPorts, dnsServers, hostnames)
SELECT n.itemID, n.subjectID, n.instance, n.type,
    COALESCE((SELECT l.version FROM launcher_instances l WHERE l.itemID = n.itemID AND l.subjectID = n.subjectID
        AND l.instance = n.instance AND l.type = n.type ORDER BY l.timestamp DESC LIMIT 1), ''),
    n.networkID, n.nodeID, n.ip, n.exposedPorts, n.dnsServers, n.hostnames FROM networkmanager_instances n;

DROP TABLE networkmanager_instances;
ALTER TABLE networkmanager_instances_new RENAME TO networkmanager_instances;

CREATE TABLE pending_connections_new (
    requesterItemID TEXT,
    requesterSubjectID TEXT,
    requesterInstance INTEGER,
    requesterType TEXT,
    requesterVersion TEXT NOT NULL DEFAULT '',
    nodeID TEXT,
    networkID TEXT,
    requesterIP TEXT,
    requesterSubnet TEXT,
    targetItemID TEXT,
    port TEXT,
    protocol TEXT
);

INSERT INTO pending_connections_new (requesterItemID, requesterSubjectID, requesterInstance, requesterType,
    requesterVersion, nodeID, networkID, requesterIP, requesterSubnet, targetItemID, port, protocol)
SELECT p.requesterItemID, p.requesterSubjectID, p.requesterInstance, p.requesterType,
    COALESCE((SELECT l.version FROM launcher_instances l WHERE l.itemID = p.requesterItemID
        AND l.subjectID = p.requesterSubjectID AND l.instance = p.requesterInstance AND l.type = p.requesterType
        ORDER BY l.timestamp DESC LIMIT 1), ''),
    p.nodeID, p.networkID, p.requesterIP, p.requesterSubnet, p.targetItemID, p.port, p.protocol
FROM pending_connections p;

DROP TABLE pending_connections;
ALTER TABLE pending_connections_new RENAME TO pending_connections;

CREATE TABLE launcher_instances_new (
    itemID TEXT,
    subjectID TEXT,
    instance INTEGER,
    type TEXT,
    manifestDigest TEXT,
    nodeID TEXT,
    prevNodeID TEXT,
    runtimeID TEXT,
    uid INTEGER,
    gid INTEGER,
    timestamp INTEGER,
    state TEXT,
    isUnitSubject INTEGER,
    version TEXT NOT NULL DEFAULT '',
    ownerID TEXT,
    subjectType TEXT,
    labels TEXT,
    priority INTEGER,
    disableRebalancing INTEGER,
    indexDigest TEXT NOT NULL DEFAULT '',
    PRIMARY KEY(itemID, subjectID, instance, type, version)
);

INSERT OR REPLACE INTO launcher_instances_new (itemID, subjectID, instance, type, manifestDigest, nodeID,
    prevNodeID, runtimeID, uid, gid, timestamp, state, isUnitSubject, version, ownerID, subjectType, labels,
    priority, disableRebalancing, indexDigest)
SELECT itemID, subjectID, instance, type, manifestDigest, nodeID, prevNodeID, runtimeID, uid, gid, timestamp, state,
    isUnitSubject, COALESCE(version, ''), ownerID, subjectType, labels, priority, disableRebalancing, indexDigest
FROM launcher_instances;

DROP TABLE launcher_instances;
ALTER TABLE launcher_instances_new RENAME TO launcher_instances;

UPDATE SchemaVersion SET version = 3;

COMMIT;
