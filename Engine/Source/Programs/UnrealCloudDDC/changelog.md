# Unreleased

# 1.2.0
* `InlineMaxBlobSize` option has moved from `Scylla` to `UnrealCloudDDC` as this can now be used to generally control if blob are inlined into the ref store (minimal practical difference as only Scylla supports inlining blobs). Resolves issue with `EnablePutRefBodyIntoBlobStore` option which will prevent these inlined blobs from also being added to the blob store.
* Added experimental option that is disabled by default to allow enumeration of buckets (enabled per namespace). Could potentially be used by oplogs.
* Optimizations for deleting blobs from multiple namespaces at once.
* Blob replication method added, can be enabled by setting the `Version` option under each replicator to `Blobs` - this will likely be made the default in future releases. The blob based replication is faster and more reliable then our old speculative replication as this has a more explicit list of which blobs to replicate rather then the previous method that was more indirect via the submitted ref (which can be mutated). This is also more useful for oplogs where a ref has a lot of references but few changed blobs each upload.
* Added support for new blob endpoint under refs as used in UE 5.6
* Added configuration for HPA (Horizontal pod autoscaler) in Helm chart. We use this to autoscale Cloud DDC instance a bit during bursty periods (in combination with a node autoscaler).
* Added option `AllowedNamespaces` on a authentication scheme to limit which namespaces a scheme is allowed to grant access to. This can be useful if you have a 3rd party authentication server you want to use but do not control and only want to grant access to some data for that party. For most use cases claims should be sufficent to control the access you need to grant.

# 1.1.1
* Fixes to helm chart when using ServiceAccounts for authentication and configuring replication

# 1.1.0
* Added ability to specify a access token if using ServiceAccounts to the ServiceCredentials (allowing for replication to function when no OIDC configuration is present)
* Fixed issue with blob stats using statements which did not work for CosmosDB by disabling them which means blob stats will not function for CosmosDB (this feature is not enabled by default anyway)
* Added ability to define a pod annotation that contains a checksum of the configmap, causing pods to get restarted when the config changes.
* Fixed issues with batch endpoints for compressed blob endpoints, not used by any production workloads yet.
* Speed up S3 blob listing for GC by listing them per S3 prefix. Slower for very small datasets were speed doesn't matter. Can be disabled with `S3.PerPrefixListing`
* Changed filesystem cleanup to just delete objects that are quite old rather then finding the oldest, makes it much more responsive and needs less state in memory at the cost of the cleanup being a bit more random but this is just the filecache anyway.
* Fixes to ondemand replication to avoid infinite recursion.

# 1.0.0
* .NET 8 Upgrade.
* Fixes for very large payloads (2GB+)
* Bug fixes for replication, mostly fixes to improve behavior when replication has fallen behind.
* Reduced GC pressure when processing a lot of new blobs that could cause very long stalls randomly (up to 1s response times).
* Speed up GC of refs when using Scylla.
* Tweaks to nginx configuration when using it as a reverse proxy.
* Reference store consistency check added, enabled via `ConsistencyCheck:EnableRefStoreChecks`. Can help repair inconsitencies between tables in Scylla which could happen due to bugs in earlier versions (resulting in data that is never garbage collected).
* Endpoints for serving symbol data that is compatible with MS Symbol server http api (but still requires auth which is not supported by Visual Studio out of the box). Experimental feature.

# 0.6.0
* Added option to track per bucket stats `EnableBucketStatsTracking`, this is still WIP.
* Added option to tweak nginx keep alive connections and increased it, only applies if using the nginx proxy.
* Added option to control TCP backlog in kestrel.
* Improved error message when no keyspace is set to Scylla
* Increased proxy timeout when using nginx, allowing for long operations (upload of oplogs) to not timeout.
* Added option to disable chunk encoding `S3.UseChunkEncoding` which needs to be set when using GCS.

# 0.5.2
* Fixed issue in helm charts defaulting to a incorrect docker registry path for the worker deployment.
* Added ability to configure and bumped number of keepalive connections in nginx, resolves issues during large spikes of traffic.

# 0.5.1
* Fixed issue in helm charts defaulting to a incorrect docker registry path.

# 0.5.0
* Fixed issue introduced in `0.4.0` that would cause last access tracking to not work correctly.
* Ability to run with auth enabled but using only the service account scheme - useful for a simpler setup as that does not require setup specific information.
* Improvments to scylla requests to avoid churning the scylla cache when doing GC.
* Fixed bug in the ref memory cache were overwrites would not correctly updated the local cache.
* Tweaks to scylla node connection limits to allow for more requests per connection.

# 0.4.0
* *Breaking* The scylla connection string now needs to include the default keyspace, a example connection string is  `Contact Points=your-scylla-dns.your-domain.com;;Default Keyspace=jupiter;`. The keyspace is `jupiter` is you are migrating from older releases. This allows you to also set the keyspace to something different if you want to run multiple instances of Unreal Cloud DDC against the same scylla cluster.)
* Migration options from `0.3.0` have been updated to assume you have migrated by default.
* Added `prepareNvmeFilesystem` section in Helm chart that creates a initContainer which will format a attached nvme drive.
* Fixed issue when content id remapped due to a new version, its supposed to use the smaller version but was in fact using the larger.
* Bug fixes to make the speculative replication more resilient.
* Ability to use nginx to sanatize http traffic and only use a level 4 load balancer in front as a performance improvment.
* Fixes for handling 2GB+ files.
* Fixes for on-demand replication.
* Support for S3 multipart uploads of large files.
* `Scylla.AvoidSchemaChanges` can now be set to avoid triggering schema modifications - this forces you to manually apply any schema changes required when upgrading but also means you can avoid triggering schema changes while maintinance is running.
* Added `MetricsService` which is disabled by default, will scan all data to determine things like number of objects in each bucket and sizes. Puts a fairly heavy load on your DB so is disabled by default.
* Added GC of non-finalized refs at a more aggresive cadence then normal refs (removed when they are 2 hours old)
  
# 0.3.0
* Azure blob storage now supports storage pools
* Last access table refactoring - Moved the last accessing tracking out of the objects table and into a seperate table. Saves on compation work for Scylla. Set `Scylla.ListObjectsFromLastAccessTable` to migrate GC refs to use this new table (will be default in the next release).
* Blob index table refactoring - Multiple changes to the blob index table, avoiding collections in the scylla rows and instead prefering clustering keys. This reduces amount of work that needs to happen when updating regions or references to blobs and improves performance (and caching) on the DB. We automatically migrate old blob index entiries when `Scylla.MigrateFromOldBlobIndex` is set (it is by default). We will start requring data in these tables by the next release. 
* Bucket table refactoring - Reads data from old and new table for this release. Set `Scylla.ListObjectsFromOldNamespaceTable` to disable reading the old table.
* Generally reduced some of the excessive logging from the worker in favor of open telemetry metrics instead.
* GC Rules - Added ability to disable GC or configure it to use Time-To-Live instead of last access tracking.
* FallbackNamespaces added - allows you to configure a second namespace used for blob operations if blobs are missing from the first. This can be used to store Virtual Assets in a seperate store pool that have different GC rules.
* Added ability to use presigned urls when uploading / downloading blobs
* If using a nginx reverse proxy on the same host we can support redirecting filesystem reads to it (X-Accel).
* Fixed issue with compact binary packages not being serialized correctly

# 0.2.0
* Improved handling of cassandra timeouts during ref GC.
* Added deletion of invalid references during blob GC - this reduces the size for entries in the blob_index table.
* Using OpenTelemetry for tracing - still supports datadog traces but this can now be forwarded to any OpenTelemetry compatible service.
* Fixed issue in Azure Blob storage with namespace containing _
* Optimized content id resolving
* Fixed issue with crash on startup from background services for certain large configuration objects
* Fixed issues with content id when using CosmosDB

# 0.1.0
* First public release of UnrealCloudDDC

# Older releases
No detailed changelog provided for older releases.