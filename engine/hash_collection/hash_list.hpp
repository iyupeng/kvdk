#pragma once

#include "../dl_list.hpp"
#include "../hash_table.hpp"
#include "kvdk/types.hpp"

namespace KVDK_NAMESPACE {

class HashIteratorImpl;

struct HashWriteArgs {
  StringView collection;
  StringView key;
  StringView value;
  WriteOp op;
  HashList* hlist;
  SpaceEntry space;
  TimestampType ts;
  HashTable::LookupResult lookup_result;
};

class HashList : public Collection {
 public:
  struct WriteResult {
    Status s = Status::Ok;
    DLRecord* existing_record = nullptr;
    DLRecord* write_record = nullptr;
    HashEntry* hash_entry_ptr = nullptr;
  };

  HashList(DLRecord* header, const StringView& name, CollectionIDType id,
           Allocator* pmem_allocator, HashTable* hash_table,
           LockTable* lock_table)
      : Collection(name, id),
        dl_list_(header, pmem_allocator, lock_table),
        size_(0),
        pmem_allocator_(pmem_allocator),
        hash_table_(hash_table) {}

  ~HashList() final = default;

  DLList* GetDLList() { return &dl_list_; }

  const DLRecord* HeaderRecord() const { return dl_list_.Header(); }

  DLRecord* HeaderRecord() { return dl_list_.Header(); }

  ExpireTimeType GetExpireTime() const final {
    return HeaderRecord()->GetExpireTime();
  }

  TimestampType GetTimeStamp() const { return HeaderRecord()->GetTimestamp(); }

  bool HasExpired() const final { return HeaderRecord()->HasExpired(); }

  // Return number of valid data record in this hash list
  size_t Size() { return size_; }

  // Put "key, value" to the hash list
  //
  // Args:
  // * timestamp: kvdk engine timestamp of this operation
  //
  // Return Ok on success, with the writed data record, its dram node and
  // updated data record if it exists
  //
  // Notice: the putting key should already been locked by engine
  WriteResult Put(const StringView& key, const StringView& value,
                  TimestampType timestamp);

  // Get value of "key" from the hash list
  Status Get(const StringView& key, std::string* value);

  // Delete "key" from the hash list by replace it with a delete record
  //
  // Args:
  // * timestamp: kvdk engine timestamp of this operation
  //
  // Return Ok on success, with the writed delete record and deleted
  // record if it exists
  //
  // Notice: the deleting key should already been locked by engine
  WriteResult Delete(const StringView& key, TimestampType timestamp);

  WriteResult Modify(const StringView key, ModifyFunc modify_func,
                     void* modify_args, TimestampType timestamp);

  // Init args for put or delete operations
  HashWriteArgs InitWriteArgs(const StringView& key, const StringView& value,
                              WriteOp op);

  // Prepare neccessary resources for write, store lookup result of key and
  // required memory space to write new reocrd in args
  //
  // Args:
  // * args: generated by InitWriteArgs()
  //
  // Return:
  // Ok on success
  // MemoryOverflow if no enough kv memory space
  // MemoryOverflow if no enough dram space
  //
  // Notice: args.key should already been locked by engine
  Status PrepareWrite(HashWriteArgs& args, TimestampType ts);

  // Do batch write according to args
  //
  // Args:
  // * args: write args prepared by PrepareWrite()
  //
  // Return:
  // Status Ok on success, with the writed delete record, its dram node and
  // deleted record if existing
  WriteResult Write(HashWriteArgs& args);

  // Set this hash list expire at expired_time
  //
  // Args:
  // * expired_time: time to expire
  // * timestamp: kvdk engine timestamp of calling this function
  //
  // Return Ok on success
  WriteResult SetExpireTime(ExpireTimeType expired_time,
                            TimestampType timestamp);

  // Replace "old_record" from the hash list with "replacing_record"
  //
  // Args:
  // * old_record: existing record to be replaced
  // * new_record: new reocrd to replace the older one
  //
  // Return:
  // * true on success
  // * false if old_record not linked on a skiplist
  //
  // Notice:
  // 1. key of the replacing record should already been locked by engine
  // 2. hash table will not be modified
  bool Replace(DLRecord* old_record, DLRecord* new_record) {
    return dl_list_.Replace(old_record, new_record);
  }

  // Destroy and free the whole hash list with old version list.
  void DestroyAll();

  // Destroy and free the whole hash list of newest version records
  void Destroy();

  void UpdateSize(int64_t delta) {
    kvdk_assert(delta >= 0 || size_.load() >= static_cast<size_t>(-delta),
                "Update hash list size to negative");
    size_.fetch_add(delta, std::memory_order_relaxed);
  }

  Status CheckIndex();

  bool TryCleaningLock() { return cleaning_lock_.try_lock(); }

  void ReleaseCleaningLock() { cleaning_lock_.unlock(); }

  static CollectionIDType FetchID(const DLRecord* record);

  static bool MatchType(const DLRecord* record) {
    RecordType type = record->GetRecordType();
    return type == RecordType::HashElem || type == RecordType::HashHeader;
  }

 private:
  friend HashIteratorImpl;
  DLList dl_list_;
  std::atomic<size_t> size_;
  Allocator* pmem_allocator_;
  HashTable* hash_table_;
  // to avoid illegal access caused by cleaning skiplist by multi-thread
  SpinMutex cleaning_lock_;

  WriteResult putPrepared(const HashTable::LookupResult& lookup_result,
                          const StringView& key, const StringView& value,
                          TimestampType timestamp, const SpaceEntry& space);

  WriteResult deletePrepared(const HashTable::LookupResult& lookup_result,
                             const StringView& key, TimestampType timestamp,
                             const SpaceEntry& space);
};
}  // namespace KVDK_NAMESPACE
