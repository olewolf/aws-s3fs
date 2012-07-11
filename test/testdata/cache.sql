-- Create parent directories.
INSERT INTO parents( remotename, localname, uid, gid, permissions ) VALUES( 'http://remotedir1', 'DIR001', 1000, 1000, 493 );
INSERT INTO parents( remotename, localname, uid, gid, permissions ) VALUES( 'http://remotedir2', 'DIR002', 1001, 1001, 493 );

-- Create files.
INSERT INTO files( bucket, remotename, localname, subscriptions, parent, uid, gid, permissions ) VALUES( 'bucketname', 'http://remote1', 'FILE01', '1', '1', '1000', '1000', '420' );
INSERT INTO files( bucket, remotename, localname, subscriptions, parent, uid, gid, permissions ) VALUES( 'bucketname', 'http://remote2', 'FILE02', '1', '1', '1000', '1000', '420' );
INSERT INTO files( bucket, remotename, localname, subscriptions, parent, uid, gid, permissions ) VALUES( 'bucketname', 'http://remote3', 'FILE03', '2', '2', '1000', '1000', '420' );
INSERT INTO files( bucket, remotename, localname, subscriptions, parent, uid, gid, permissions ) VALUES( 'bucketname', 'http://remote4', 'FILE04', '1', '2', '1001', '1001', '420' );

-- Create users.
INSERT INTO users( uid, keyid, secretkey ) VALUES( '1005', '1234', '5678' );

-- Create transfer queue.
INSERT INTO transfers( owner, file, direction ) VALUES( '1005', '2', 'd' );
INSERT INTO transfers( owner, file, direction ) VALUES( '1005', '3', 'u' );

-- Create multi-upload queue.
INSERT INTO transferparts( transfer, part, inprogress, etag ) VALUES( '2', '1', '0', 'etagadlh5w4e3' );
INSERT INTO transferparts( transfer, part, inprogress, etag ) VALUES( '2', '2', '0', 'etaga398aetkh' );

