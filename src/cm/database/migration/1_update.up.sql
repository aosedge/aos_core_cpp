BEGIN TRANSACTION;

CREATE TABLE storagestate_new (
    itemID TEXT,
    version TEXT,
    subjectID TEXT,
    instance INTEGER,
    type TEXT,
    storageQuota INTEGER,
    stateQuota INTEGER,
    stateChecksum BLOB,
    PRIMARY KEY(itemID, version, subjectID, instance, type)
);

INSERT OR REPLACE INTO storagestate_new (
    itemID, version, subjectID, instance, type, storageQuota, stateQuota, stateChecksum
)
SELECT
    itemID,
    CASE
        WHEN preinstalled IS NULL THEN ''
        ELSE CAST(preinstalled AS TEXT)
    END,
    subjectID,
    instance,
    type,
    storageQuota,
    stateQuota,
    stateChecksum
FROM storagestate;

DROP TABLE storagestate;
ALTER TABLE storagestate_new RENAME TO storagestate;

CREATE TABLE networkmanager_instances_new (
    itemID TEXT,
    version TEXT,
    subjectID TEXT,
    instance INTEGER,
    type TEXT,
    networkID TEXT,
    nodeID TEXT,
    ip TEXT,
    exposedPorts TEXT,
    dnsServers TEXT,
    PRIMARY KEY(itemID, version, subjectID, instance, type),
    FOREIGN KEY(networkID) REFERENCES networks(networkID),
    FOREIGN KEY(networkID, nodeID) REFERENCES hosts(networkID, nodeID)
);

INSERT OR REPLACE INTO networkmanager_instances_new (
    itemID, version, subjectID, instance, type, networkID, nodeID, ip, exposedPorts, dnsServers
)
SELECT
    itemID,
    CASE
        WHEN preinstalled IS NULL THEN ''
        ELSE CAST(preinstalled AS TEXT)
    END,
    subjectID,
    instance,
    type,
    networkID,
    nodeID,
    ip,
    exposedPorts,
    dnsServers
FROM networkmanager_instances;

DROP TABLE networkmanager_instances;
ALTER TABLE networkmanager_instances_new RENAME TO networkmanager_instances;

CREATE TABLE pending_connections_new (
    requesterItemID TEXT,
    requesterVersion TEXT,
    requesterSubjectID TEXT,
    requesterInstance INTEGER,
    requesterType TEXT,
    nodeID TEXT,
    networkID TEXT,
    requesterIP TEXT,
    requesterSubnet TEXT,
    targetItemID TEXT,
    port TEXT,
    protocol TEXT
);

INSERT INTO pending_connections_new (
    requesterItemID, requesterVersion, requesterSubjectID, requesterInstance, requesterType, nodeID, networkID,
    requesterIP, requesterSubnet, targetItemID, port, protocol
)
SELECT
    requesterItemID,
    CASE
        WHEN requesterPreinstalled IS NULL THEN ''
        ELSE CAST(requesterPreinstalled AS TEXT)
    END,
    requesterSubjectID,
    requesterInstance,
    requesterType,
    nodeID,
    networkID,
    requesterIP,
    requesterSubnet,
    targetItemID,
    port,
    protocol
FROM pending_connections;

DROP TABLE pending_connections;
ALTER TABLE pending_connections_new RENAME TO pending_connections;

CREATE TABLE launcher_instances_new (
    itemID TEXT,
    version TEXT,
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
    ownerID TEXT,
    subjectType TEXT,
    labels TEXT,
    priority INTEGER,
    disableRebalancing INTEGER,
    PRIMARY KEY(itemID, version, subjectID, instance, type)
);

INSERT OR REPLACE INTO launcher_instances_new (
    itemID, version, subjectID, instance, type, manifestDigest, nodeID, prevNodeID, runtimeID, uid, gid, timestamp,
    state, isUnitSubject, ownerID, subjectType, labels, priority, disableRebalancing
)
SELECT
    itemID,
    version,
    subjectID,
    instance,
    type,
    manifestDigest,
    nodeID,
    prevNodeID,
    runtimeID,
    uid,
    gid,
    timestamp,
    state,
    isUnitSubject,
    ownerID,
    subjectType,
    labels,
    priority,
    disableRebalancing
FROM launcher_instances;

DROP TABLE launcher_instances;
ALTER TABLE launcher_instances_new RENAME TO launcher_instances;

COMMIT;
