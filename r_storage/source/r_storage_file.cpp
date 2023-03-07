
#include "r_storage/r_storage_file.h"
#include "r_storage/r_storage_file_reader.h"
#include "r_storage/r_rel_block.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_blob_tree.h"
#include "r_utils/r_string_utils.h"
#include "r_db/r_sqlite_conn.h"
#include <memory>
#include <algorithm>

using namespace r_utils;
using namespace r_storage;
using namespace r_db;
using namespace std;

r_storage_file::r_storage_file(const string& file_name) :
    _file(r_file::open(file_name, "r+")),
    _file_name(file_name),
    _file_lock(r_fs::fileno(_file)),
    _h(_read_header(file_name)),
    _dumbdex_map(_map_block(0)),
    _block_index(file_name, (uint8_t*)_dumbdex_map->map() + R_STORAGE_FILE_HEADER_SIZE, _h.num_blocks),
    _gop_buffer(),
    _ind_map(),
    _current_block(),
    _first_ts(),
    _last_ts(),
    _current_segment_id()
{
    auto base_name = file_name.substr(0, (file_name.find_last_of('.')));

    r_storage_file::upgrade_file(base_name);

    r_storage_file_reader reader(base_name + string(".rvd"));

    auto last_ts = reader.last_ts();

    if(!last_ts.is_null())
    {
        r_sqlite_conn conn(_file_name.substr(0, _file_name.find_last_of('.')) + string(".sdb"));
        r_sqlite_transaction(conn,[&](const r_sqlite_conn& conn){
            conn.exec(
                r_string_utils::format(
                    "UPDATE segments SET end_ts=%lld WHERE end_ts=0;",
                    last_ts.value()
                )
            );
        });
    }
}

r_storage_file::~r_storage_file() noexcept
{
}

r_storage_write_context r_storage_file::create_write_context(
    const string& video_codec_name,
    const r_nullable<string>& video_codec_parameters,
    const r_nullable<string>& audio_codec_name,
    const r_nullable<string>& audio_codec_parameters
)
{
    r_storage_write_context ctx;
    ctx.video_codec_name = video_codec_name;
    ctx.video_codec_parameters = (!video_codec_parameters.is_null())?video_codec_parameters.value():"";
    ctx.audio_codec_name = (!audio_codec_name.is_null())?audio_codec_name.value():"";
    ctx.audio_codec_parameters = (!audio_codec_parameters.is_null())?audio_codec_parameters.value():"";
    return ctx;
}

void r_storage_file::write_frame(const r_storage_write_context& ctx, r_storage_media_type media_type, const uint8_t* p, size_t size, bool key, int64_t ts, int64_t pts)
{
    r_file_lock_guard g(_file_lock);

    if(_first_ts.is_null())
    {
        _first_ts.set_value(ts);

        r_sqlite_conn conn(_file_name.substr(0, _file_name.find_last_of('.')) + string(".sdb"));
        r_sqlite_transaction(conn,[&](const r_sqlite_conn& conn){
            conn.exec(
                r_string_utils::format(
                    "INSERT INTO segments(start_ts, end_ts) VALUES(%lld, 0);",
                    _first_ts.value()
                )
            );

            _current_segment_id.set_value(conn.last_insert_id());
        });
    }

    _last_ts.set_value(ts);

    if(media_type >= R_STORAGE_MEDIA_TYPE_ALL)
        R_THROW(("Invalid storage media type."));

    if(key)
    {
        for(auto& gop : _gop_buffer)
        {
            // mark any existing incomplete gop's of the same type as complete
            if(gop.media_type == media_type && gop.complete == false)
                gop.complete = true;
        }

        _gop g;
        g.complete = false;
        g.ts = ts;
        g.data.resize(size + r_rel_block::PER_FRAME_OVERHEAD);
        g.media_type = media_type;
        r_rel_block::append(g.data.data(), p, size, ts, 1);
        // We use upper_bound() here because if a gop with the same ts already exists we want to go after it (so, first gop in wins)
        _gop_buffer.insert(upper_bound(_gop_buffer.begin(), _gop_buffer.end(), g, [](const _gop& a, const _gop& b){return a.ts < b.ts;}), g);
    }
    else
    {
        // find our incomplete gop of this media_type and append this frame to it.
        auto found = find_if(
            rbegin(_gop_buffer),
            rend(_gop_buffer), 
            [media_type](const _gop& g) {return g.media_type == media_type && g.complete == false;}
        );
        if(found == rend(_gop_buffer))
            R_THROW(("No incomplete GOP found for media type."));

        auto current_size = found->data.size();
        auto new_size = current_size + size + r_rel_block::PER_FRAME_OVERHEAD;

        if(new_size > _h.block_size)
            R_THROW(("GOP is larger than our storage block size!"));

        if(found->data.capacity() < new_size)
            found->data.reserve(new_size * 2);

        found->data.resize(new_size);

        r_rel_block::append(&found->data[current_size], p, size, ts, 0);
    }

    // Ok, so I added a "_gop.complete==true" check in _buffer_full(). This will hopefully prevent us from getting stuck
    // in an infinite loop with an incomplete gop that will never be completed. Also, in the above code we mark any
    // incomplete gop's as complete when we insert a new key frame... SO, even if we dropped a frame at the network
    // layer we should not get stuck here as long as subsequent gops are completed.

    while(_buffer_full())
    {
        auto gb_iter = _gop_buffer.begin();

        if(gb_iter->complete)
        {
            if(!_current_block || !_current_block->fits(gb_iter->data.size()))
            {
                // use this opportunity to delete any segments older than the oldest frame in our rvd.
                r_storage_file_reader reader(_file_name);

                // Use the _ version of _first_ts() here because we already have the lock.
                auto first_ts = reader._first_ts();

                if(!first_ts.is_null())
                    _delete_segments_older_than(first_ts.value());

                // How should we actually do it?
                // When we are setting up camera for recording and we stream a bit for 10 seconds... Count how many
                // independent gops we encounter (video gops + audio buffers). Divide that sum by 10 seconds to
                // know how many ind blocks per second we need. Now, divide the storage block size by the bitrate
                // and multiply that number by the indexes per second. That is approx how many indexes we need per
                // ind block.

                auto blk_idx = _block_index.insert(gb_iter->ts);

                _current_block = _initialize_ind_block(ctx, blk_idx, gb_iter->ts, 2000);
            }

            _current_block->append(gb_iter->data.data(), gb_iter->data.size(), gb_iter->media_type, gb_iter->ts);

            _gop_buffer.erase(gb_iter);
        }
    }
}

void r_storage_file::finalize(const r_storage_write_context& ctx)
{
    r_file_lock_guard g(_file_lock);

    if(!_current_segment_id.is_null())
    {
        r_sqlite_conn conn(_file_name.substr(0, _file_name.find_last_of('.')) + string(".sdb"));

        r_sqlite_transaction(conn,[&](const r_sqlite_conn& conn){
            conn.exec(
                r_string_utils::format(
                    "UPDATE segments SET end_ts=%s WHERE id=%s;",
                    r_string_utils::int64_to_s((_last_ts.is_null())?0:_last_ts.value()).c_str(),
                    _current_segment_id.value().c_str()
                )
            );
        });
    }

    while(!_gop_buffer.empty())
    {
        _gop g = _gop_buffer.front();
        _gop_buffer.pop_front();

        if(!_current_block || !_current_block->fits(g.data.size()))
            _current_block = _initialize_ind_block(ctx, _block_index.insert(g.ts), g.ts, _h.block_size / g.data.size());
    
        _current_block->append(g.data.data(), g.data.size(), g.media_type, g.ts);
    }
}

r_nullable<map<string, r_nullable<string>>> _query_one_segment(r_sqlite_conn& conn, const std::string& query)
{
    auto rows = conn.exec(query);

    if(rows.size() == 0)
        return r_nullable<map<string, r_nullable<string>>>();

    return r_nullable<map<string, r_nullable<string>>>(rows[0]);
}

size_t r_storage_file::remove_blocks(int64_t start_ts, int64_t end_ts)
{
    r_file_lock_guard g(_file_lock);

#if 0
    {
        auto i = _block_index.begin();
        while(i != _block_index.end())
        {
            printf("    block_index: %ld\n", (*i).first);
            i.next();
        }
    }
#endif

    vector<int64_t> index_blocks_to_delete;

    auto block_start = _block_index.find_lower_bound(start_ts);

    auto block_end = _block_index.find_lower_bound(end_ts);

    if(block_start.valid() && block_end.valid() && (block_start != block_end))
    {
        r_nullable<int64_t> del_start_ts;

        while(block_start != block_end)
        {
            auto block_start_ts = (*block_start).first;
            if(del_start_ts.is_null())
                del_start_ts.set_value(block_start_ts);
            index_blocks_to_delete.push_back(block_start_ts);
            block_start.next();
        }

        r_nullable<int64_t> del_end_ts;

        if(!block_end.valid())
            R_THROW(("remove_blocks() block_end is invalid."));

        del_end_ts.set_value((*block_end).first);

        r_sqlite_conn conn(_file_name.substr(0, _file_name.find_last_of('.')) + string(".sdb"));

        auto containing_segment = _query_one_segment(
            conn,
            r_string_utils::format(
                "SELECT * FROM segments WHERE %s > start_ts AND %s < end_ts;",
                r_string_utils::int64_to_s(del_start_ts.value()).c_str(),
                r_string_utils::int64_to_s(del_end_ts.value()).c_str()
            )
        );

        if(containing_segment.is_null())
        {
            r_sqlite_transaction(conn,[&](const r_sqlite_conn& conn){
                // - For all segments whose end_ts > del_start_ts and end_ts < del_end_ts
                //   - if the start_ts < del_start_ts
                //     - Update the segment's end_ts to del_start_ts
                //   - Else delete the segment
                auto result = conn.exec(
                    r_string_utils::format(
                        "SELECT * FROM segments WHERE end_ts >= %s AND end_ts < %s;",
                        r_string_utils::int64_to_s(del_start_ts.value()).c_str(),
                        r_string_utils::int64_to_s(del_end_ts.value()).c_str()
                    )
                );

                for(auto row : result)
                {
                    int64_t seg_start_ts = r_string_utils::s_to_int64(row.at("start_ts").value());
                    int64_t seg_end_ts = r_string_utils::s_to_int64(row.at("end_ts").value());

                    if(seg_start_ts < del_start_ts.value())
                    {
                        conn.exec(
                            r_string_utils::format(
                                "UPDATE segments SET end_ts=%s WHERE id=%s;",
                                r_string_utils::int64_to_s(del_start_ts.value()).c_str(),
                                row.at("id").value().c_str()
                            )
                        );
                    }
                    else
                    {
                        conn.exec(
                            r_string_utils::format(
                                "DELETE FROM segments WHERE id=%s;",
                                row.at("id").value().c_str()
                            )
                        );
                    }
                }

                // - For all segments whose start_ts > del_start_ts and start_ts < del_end_ts
                //   - update segment's start_ts to del_end_ts

                result = conn.exec(
                    r_string_utils::format(
                        "SELECT * FROM segments WHERE start_ts >= %s and start_ts < %s;",
                        r_string_utils::int64_to_s(del_start_ts.value()).c_str(),
                        r_string_utils::int64_to_s(del_end_ts.value()).c_str()
                    )
                );

                for(auto row : result)
                {
                    conn.exec(
                        r_string_utils::format(
                            "UPDATE segments SET start_ts=%s WHERE id=%s;",
                            r_string_utils::int64_to_s(del_end_ts.value()).c_str(),
                            row.at("id").value().c_str()
                        )
                    );
                }
            });
        }
        else
        {
            // segment splitting case
            // - Update the segment's end_ts to del_start_ts
            // - Create a new segment with start_ts = del_end_ts and end_ts = old_end_ts
            // - Delete the block from the index

            r_sqlite_transaction(conn,[&](const r_sqlite_conn& conn){
                string seg_id = containing_segment.value().at("id").value();

                conn.exec(
                    r_string_utils::format(
                        "UPDATE segments SET end_ts=%s WHERE id=%s;",
                        r_string_utils::int64_to_s(del_start_ts.value()).c_str(),
                        seg_id.c_str()
                    )
                );

                conn.exec(
                    r_string_utils::format(
                        "INSERT INTO segments(start_ts, end_ts) VALUES(%s, %s);",
                        r_string_utils::int64_to_s(del_end_ts.value()).c_str(),
                        containing_segment.value().at("end_ts").value().c_str()
                    )
                );
            });
        }

        for(auto del_ts : index_blocks_to_delete)
        {
            printf("REMOVING BLOCK @ %ld\n", del_ts);
            _block_index.remove(del_ts);
        }
    }

    return index_blocks_to_delete.size();
}

void r_storage_file::allocate(const std::string& file_name, size_t block_size, size_t num_blocks)
{
    r_sqlite_conn conn(file_name.substr(0, file_name.find_last_of('.')) + string(".sdb"));

    r_sqlite_transaction(conn,[&](const r_sqlite_conn& conn){
        conn.exec("CREATE TABLE segments(start_ts INTEGER, end_ts INTEGER);");
        conn.exec("CREATE INDEX segments_start_ts_idx ON segments(start_ts);");
    });

    {
#ifdef IS_WINDOWS
        FILE* fp = nullptr;
        fopen_s(&fp, file_name.c_str(), "w+");
#endif
        std::unique_ptr<FILE, decltype(&fclose)> f(
#ifdef IS_WINDOWS
            fp,
#endif
#ifdef IS_LINUX
            fopen(file_name.c_str(), "w+"),
#endif
            &fclose
        );

        if(!f)
            R_THROW(("r_storage_file: unable to create r_storage_file file."));

        if(r_fs::fallocate(f.get(), num_blocks * block_size) < 0)
            R_THROW(("r_storage_file: unable to allocate file."));

        fflush(f.get());
    }

    {
#ifdef IS_WINDOWS
        FILE* fp = nullptr;
        fopen_s(&fp, file_name.c_str(), "r+");
#endif
        std::unique_ptr<FILE, decltype(&fclose)> f(
#ifdef IS_WINDOWS
            fp,
#endif
#ifdef IS_LINUX
            fopen(file_name.c_str(), "r+"),
#endif
            &fclose
        );

        if(!f)
            throw std::runtime_error("dumbdex: unable to open dumbdex file.");

        r_memory_map mm(
            r_fs::fileno(f.get()),
            0,
            (uint32_t)block_size,
            r_memory_map::RMM_PROT_READ | r_memory_map::RMM_PROT_WRITE,
            r_memory_map::RMM_TYPE_FILE | r_memory_map::RMM_SHARED
        );

        uint8_t* p = (uint8_t*)mm.map();
        ::memset(p, 0, R_STORAGE_FILE_HEADER_SIZE);

        *(uint32_t*)p = (uint32_t)(num_blocks-1);
        *(uint32_t*)(p+4) = (uint32_t)block_size;

        r_dumbdex::allocate(
            file_name,
            p + R_STORAGE_FILE_HEADER_SIZE,
            block_size - R_STORAGE_FILE_HEADER_SIZE,
            num_blocks - 1
        );

        fflush(f.get());
    }
}

pair<int64_t, int64_t> r_storage_file::required_file_size_for_retention_hours(int64_t retention_hours, int64_t byte_rate)
{
    // Note: windows mmap implementation requires mapped regions to be a multiple of 65536.
    const int64_t FIFTY_MB_FILE = 52428800;
    const int64_t FUDGE_FACTOR = 2;

    int64_t natural_byte_size = (byte_rate * 60 * 60 * retention_hours);

    int64_t num_blocks = (natural_byte_size / FIFTY_MB_FILE) + FUDGE_FACTOR;

    return make_pair(num_blocks, FIFTY_MB_FILE);
}

string r_storage_file::human_readable_file_size(double size)
{
    int i = 0;
    const char* units[] = {"bytes", "kB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB"};
    while (size > 1024) {
        size /= 1024;
        i++;
    }

    return r_string_utils::format("%.2f %s", size, units[i]);
}

int r_storage_file::get_file_version(const std::string& friendly_name)
{
    auto db_file_name = r_string_utils::format("%s.sdb", friendly_name.c_str());

    r_sqlite_conn conn(db_file_name);

    auto maybe_value = r_db::to_scalar(conn.exec("PRAGMA user_version;"));

    if(maybe_value.is_null())
        R_THROW(("Unable to read user_version from %s", db_file_name.c_str()));
    else return r_string_utils::s_to_int(maybe_value.value());
}

void r_storage_file::set_file_version(const std::string& friendly_name, int version)
{
    auto db_file_name = r_string_utils::format("%s.sdb", friendly_name.c_str());

    r_sqlite_conn conn(db_file_name);

    conn.exec(r_string_utils::format("PRAGMA user_version = %d;", version));
}

void r_storage_file::upgrade_file(const std::string& friendly_name)
{
    auto current_version = r_storage_file::get_file_version(friendly_name);

    switch(current_version)
    {
        case 0:
        {
            auto db_file_name = r_string_utils::format("%s.sdb", friendly_name.c_str());

            r_sqlite_conn conn(db_file_name);

            r_sqlite_transaction(conn,[&](const r_sqlite_conn& conn){
                conn.exec("CREATE TABLE new_segments(id INTEGER PRIMARY KEY AUTOINCREMENT, start_ts INTEGER, end_ts INTEGER);");
                conn.exec("INSERT INTO new_segments(start_ts, end_ts) SELECT start_ts, end_ts FROM segments;");
                conn.exec("DROP TABLE segments;");
                conn.exec("ALTER TABLE new_segments RENAME TO segments;");
                conn.exec("CREATE INDEX segments_start_ts_idx ON segments(start_ts);");
            });

            r_storage_file::set_file_version(friendly_name, 1);
        }
    }
}

r_storage_file::_storage_file_header r_storage_file::_read_header(const std::string& file_name)
{
    auto tmp_f = r_file::open(file_name, "r+");
    _storage_file_header h;
    h.num_blocks = 0;
    fread(&h.num_blocks, 1, sizeof(uint32_t), tmp_f);
    h.block_size = 0;
    fread(&h.block_size, 1, sizeof(uint32_t), tmp_f);

    return h;
}

shared_ptr<r_memory_map> r_storage_file::_map_block(uint16_t block)
{
    if(block > _h.num_blocks)
        R_THROW(("Invalid block index."));

    return make_shared<r_memory_map>(
        r_fs::fileno(_file),
        ((int64_t)block) * ((int64_t)_h.block_size),
        _h.block_size,
        r_memory_map::RMM_PROT_READ | r_memory_map::RMM_PROT_WRITE,
        r_memory_map::RMM_TYPE_FILE | r_memory_map::RMM_SHARED
    );
}

shared_ptr<r_ind_block> r_storage_file::_initialize_ind_block(const r_storage_write_context& ctx, uint16_t block, int64_t ts, size_t n_indexes)
{
    if(_ind_map)
        _ind_map->flush();

    _ind_map = _map_block(block);

    _ind_map->advise(r_memory_map::RMM_ADVICE_RANDOM);

    auto p = (uint8_t*)_ind_map->map();

    r_ind_block::initialize_block(
        p,
        _h.block_size,
        (uint32_t)n_indexes,
        ts,
        ctx.video_codec_name,
        ctx.video_codec_parameters,
        ctx.audio_codec_name,
        ctx.audio_codec_parameters
    );

    return make_shared<r_ind_block>(p, _h.block_size);
}

bool r_storage_file::_buffer_full() const
{
    if(_gop_buffer.size() < 2)
        return false;

    auto b = _gop_buffer.begin();
    auto rb = _gop_buffer.rbegin();

    return ((rb->ts - b->ts) > 20000) && b->complete;
}

void r_storage_file::_delete_segments_older_than(int64_t ts)
{
    r_sqlite_conn conn(_file_name.substr(0, _file_name.find_last_of('.')) + string(".sdb"));

    r_sqlite_transaction(conn,[&](const r_sqlite_conn& conn){
        conn.exec(
            r_string_utils::format(
                "DELETE FROM segments WHERE end_ts <> 0 AND start_ts < %s;",
                r_string_utils::int64_to_s(ts).c_str()
            )
        );
    });
}
