/*
 * torb_natives.h - generated from the natives manifest by `torb natives --header`. Do not edit.
 *
 * One prototype per `native` declaration of `std/` whose target is a function of the runtime and whose state is
 * ready. Include it after torb.h: the C compiler then compares the two declarations of every symbol, so the
 * manifest (compiler/src/backend/c/natives.trb) and the runtime cannot drift.
 */

#ifndef TORB_NATIVES_H
#define TORB_NATIVES_H

#include "torb.h"

/* UInt64.addedWrapping */
uint64_t torb_added_wrapping_u64(uint64_t first, uint64_t second);
/* blockingTurn */
torb_task *torb_blocking_turn(void);
/* Float64.ceiling */
double torb_ceiling_f64(double value);
/* closeReading */
void torb_channel_close(torb_channel *channel);
/* endWriting */
void torb_channel_end(torb_channel *channel);
/* offered */
torb_task *torb_channel_offered(torb_channel *channel, const void *item);
/* received */
torb_task *torb_channel_received(torb_channel *channel);
/* Char.byteLength */
int64_t torb_char_byte_length_of(torb_char character);
/* Char.isDigit */
bool torb_char_is_digit(torb_char character);
/* Char.isLetter */
bool torb_char_is_letter(torb_char character);
/* Char.isWhitespace */
bool torb_char_is_whitespace(torb_char character);
/* Char.toLowerCase */
torb_char torb_char_to_lower_case(torb_char character);
/* Char.toUpperCase */
torb_char torb_char_to_upper_case(torb_char character);
/* Char.tryFrom(Int64) */
bool torb_char_try_from_i64(int64_t value, torb_char *out, torb_text *message);
/* Clock.milliseconds */
int64_t torb_clock_milliseconds(void);
/* monotonicNanoseconds */
int64_t torb_clock_now(void);
/* Float64.compare */
int32_t torb_compare_f64(double first, double second);
/* Int64.tryFrom(Float64) */
bool torb_convert_f64_i64_checked(double value, int64_t *out, torb_text *message);
/* Int16.tryFrom(Int64) */
bool torb_convert_i64_i16_checked(int64_t value, int16_t *out, torb_text *message);
/* Int32.tryFrom(Int64) */
bool torb_convert_i64_i32_checked(int64_t value, int32_t *out, torb_text *message);
/* Int8.tryFrom(Int64) */
bool torb_convert_i64_i8_checked(int64_t value, int8_t *out, torb_text *message);
/* UInt16.tryFrom(Int64) */
bool torb_convert_i64_u16_checked(int64_t value, uint16_t *out, torb_text *message);
/* UInt32.tryFrom(Int64) */
bool torb_convert_i64_u32_checked(int64_t value, uint32_t *out, torb_text *message);
/* UInt64.tryFrom(Int64) */
bool torb_convert_i64_u64_checked(int64_t value, uint64_t *out, torb_text *message);
/* UInt8.tryFrom(Int64) */
bool torb_convert_i64_u8_checked(int64_t value, uint8_t *out, torb_text *message);
/* Int64.tryFrom(UInt64) */
bool torb_convert_u64_i64_checked(uint64_t value, int64_t *out, torb_text *message);
/* Environment.entries */
void torb_environment_entries(torb_list *names, torb_list *values);
/* Environment.get */
bool torb_environment_get(torb_text name, torb_text *out);
/* File.absolutePath */
bool torb_file_absolute_path(torb_text path, torb_text *out, torb_text *error);
/* File.close */
void torb_file_close(torb_file **self);
/* File.createDirectory */
bool torb_file_create_directory(torb_text path, torb_text *error);
/* File.exists */
bool torb_file_exists(torb_text path);
/* File.isDirectory */
bool torb_file_is_directory(torb_text path);
/* File.list */
bool torb_file_list(torb_text path, torb_list *out, torb_text *error);
/* File.open */
bool torb_file_open(torb_text path, torb_file **out, torb_text *error);
/* File.readAll */
bool torb_file_read_all(torb_file **self, torb_text *out, torb_text *path, torb_text *error);
/* File.readText */
bool torb_file_read_text(torb_text path, torb_text *out, torb_text *error);
/* File.writeText */
bool torb_file_write_text(torb_text path, torb_text text, torb_text *error);
/* Float64.floor */
double torb_floor_f64(double value);
/* Bool.hash */
uint64_t torb_hash_bool(bool value);
/* Char.hash */
uint64_t torb_hash_char(torb_char character);
/* combineHashes */
uint64_t torb_hash_combine(uint64_t first, uint64_t second);
/* Int16.hash, Int32.hash, Int64.hash, Int8.hash */
uint64_t torb_hash_i64(int64_t value);
/* UInt16.hash, UInt32.hash, UInt64.hash, UInt8.hash */
uint64_t torb_hash_u64(uint64_t value);
/* Float64.isNaN */
bool torb_is_nan_f64(double value);
/* ArrayList.append, TrieList.append */
void torb_list_add(torb_list *list, const void *value);
/* ArrayList.clear, TrieList.clear */
void torb_list_clear(torb_list *list);
/* ArrayList.compact, TrieList.compact */
void torb_list_compact(torb_list *list);
/* ArrayList.get, TrieList.get */
bool torb_list_get(torb_list list, int64_t index, void *out);
/* ArrayList.insert, TrieList.insert */
void torb_list_insert(torb_list *list, int64_t index, const void *value, torb_location at);
/* ArrayList.length, TrieList.length */
int64_t torb_list_length(torb_list list);
/* ArrayList.removeAt, TrieList.removeAt */
bool torb_list_remove_at(torb_list *list, int64_t index, void *out);
/* ArrayList.replaceBetween, TrieList.replaceBetween */
void torb_list_replace(torb_list *list, int64_t from, int64_t to, torb_list values, torb_location at);
/* ArrayList.reverse, TrieList.reverse */
void torb_list_reverse(torb_list *list);
/* ArrayList.set, TrieList.set */
void torb_list_set(torb_list *list, int64_t index, const void *value, torb_location at);
/* ArrayList.sliceBetween, TrieList.sliceBetween */
torb_list torb_list_slice(torb_list list, int64_t from, int64_t to, torb_location at);
/* Machine.install */
void torb_machine_install(torb_closure interpreter);
/* Machine.load */
int64_t torb_machine_load(int64_t address);
/* Machine.operate */
int64_t torb_machine_operate(torb_list *words, int64_t base, torb_list code, int64_t at);
/* Machine.placeFloat */
void torb_machine_place_float(torb_list *words, int64_t at, double value);
/* Machine.placeText */
void torb_machine_place_text(torb_list *words, int64_t at, torb_text text);
/* Machine.store */
void torb_machine_store(int64_t address, int64_t value);
/* HashMap.clear, TrieMap.clear */
void torb_map_clear(torb_map *map);
/* HashMap.entryAfter, TrieMap.entryAfter */
bool torb_map_entry_after(torb_map map, int64_t *cursor, void *key, void *value);
/* HashMap.get, TrieMap.get */
bool torb_map_get(torb_map map, const void *key, void *out);
/* HashMap.length, TrieMap.length */
int64_t torb_map_length(torb_map map);
/* HashMap.remove, TrieMap.remove */
bool torb_map_remove(torb_map *map, const void *key, void *out);
/* HashMap.set, TrieMap.set */
void torb_map_set(torb_map *map, const void *key, const void *value);
/* arcCosine */
double torb_math_arc_cosine(double value);
/* arcSine */
double torb_math_arc_sine(double value);
/* arcTangent */
double torb_math_arc_tangent(double value);
/* arcTangent2 */
double torb_math_arc_tangent2(double y, double x);
/* cosine */
double torb_math_cosine(double value);
/* exponential */
double torb_math_exponential(double value);
/* logarithm */
double torb_math_logarithm(double value, double base);
/* naturalLog */
double torb_math_natural_log(double value);
/* power */
double torb_math_power(double base, double exponent);
/* sine */
double torb_math_sine(double value);
/* tangent */
double torb_math_tangent(double value);
/* UInt64.multipliedWrapping */
uint64_t torb_multiplied_wrapping_u64(uint64_t first, uint64_t second);
/* networkAccept */
torb_task *torb_network_accept(int64_t listener);
/* networkAddress */
int64_t torb_network_address(int64_t handle, bool peer, torb_list *parts);
/* networkClose */
void torb_network_close(int64_t handle);
/* networkConnect */
torb_task *torb_network_connect(int64_t family, int64_t high, int64_t low, int64_t port);
/* networkErrorText */
torb_text torb_network_error_text(int64_t failure);
/* networkListen */
int64_t torb_network_listen(int64_t family, int64_t high, int64_t low, int64_t port, int64_t backlog);
/* networkReceive */
torb_task *torb_network_receive(int64_t stream, int64_t maximum);
/* networkResolve */
torb_task *torb_network_resolve(torb_text host);
/* networkSend */
torb_task *torb_network_send(int64_t stream, torb_list bytes, int64_t from);
/* networkShutdown */
int64_t torb_network_shutdown(int64_t stream);
/* networkTakeReceived */
void torb_network_take_received(int64_t stream, torb_list *into);
/* networkTakeResolved */
void torb_network_take_resolved(int64_t resolution, torb_list *parts);
/* Bsd.sysctlInteger */
int64_t torb_os_bsd_sysctl_integer(torb_text name, int64_t *number, torb_text *failure);
/* Bsd.sysctlText */
int64_t torb_os_bsd_sysctl_text(torb_text name, torb_text *text, torb_text *failure);
/* Bsd.uptime */
int64_t torb_os_bsd_uptime(int64_t *milliseconds, torb_text *failure);
/* Linux.readSystemFile */
int64_t torb_os_linux_read_system_file(torb_text path, torb_text *text, torb_text *failure);
/* MacOs.userTemporaryDirectory */
int64_t torb_os_macos_user_temporary_directory(torb_text *path, torb_text *failure);
/* Posix.account */
int64_t torb_os_posix_account(int64_t *identifier, torb_text *name, torb_text *full_name, torb_text *home, torb_text *failure);
/* Posix.configuration */
int64_t torb_os_posix_configuration(torb_text name);
/* Posix.effectiveUserIdentifier */
int64_t torb_os_posix_effective_user_identifier(void);
/* Posix.hostName */
int64_t torb_os_posix_host_name(torb_text *name, torb_text *failure);
/* Posix.systemNames */
int64_t torb_os_posix_system_names(torb_text *system, torb_text *node, torb_text *release, torb_text *version, torb_text *machine, torb_text *failure);
/* Windows.computerName */
int64_t torb_os_windows_computer_name(torb_text *name, torb_text *failure);
/* Windows.knownFolder */
int64_t torb_os_windows_known_folder(torb_text identifier, torb_text *path, torb_text *failure);
/* Windows.registryInteger */
int64_t torb_os_windows_registry_integer(torb_text key, torb_text value, int64_t *number, torb_text *failure);
/* Windows.registryText */
int64_t torb_os_windows_registry_text(torb_text key, torb_text value, torb_text *text, torb_text *failure);
/* Windows.systemInformation */
int64_t torb_os_windows_system_information(int64_t *page_size, int64_t *architecture, int64_t *logical, torb_text *failure);
/* Windows.temporaryDirectory */
int64_t torb_os_windows_temporary_directory(torb_text *path, torb_text *failure);
/* Windows.tickCount */
int64_t torb_os_windows_tick_count(void);
/* Windows.version */
int64_t torb_os_windows_version(int64_t *major, int64_t *minor, int64_t *build, torb_text *failure);
/* panic */
void torb_panic(torb_text message, torb_location at);
/* Float64.tryFrom(String) */
bool torb_parse_f64(torb_text text, double *out);
/* Int16.tryFrom(String), Int32.tryFrom(String), Int64.tryFrom(String), Int8.tryFrom(String) */
bool torb_parse_i64(torb_text text, int64_t *out);
/* Int64.parseDigits */
bool torb_parse_i64_digits(torb_text text, int64_t radix, int64_t *out);
/* UInt16.tryFrom(String), UInt32.tryFrom(String), UInt64.tryFrom(String), UInt8.tryFrom(String) */
bool torb_parse_u64(torb_text text, uint64_t *out);
/* pause */
torb_task *torb_pause(void);
/* printError */
void torb_print_error_parts(const torb_text *parts, size_t count);
/* print */
void torb_print_parts(const torb_text *parts, size_t count);
/* Process.arguments */
torb_list torb_process_arguments(void);
/* Process.executablePath */
bool torb_process_executable_path(torb_text *out);
/* Process.exit */
void torb_process_exit(int64_t code);
/* Process.runCollecting */
int64_t torb_process_run(torb_text command, torb_list arguments, torb_text *output, torb_text *failure);
/* Process.runFeeding */
int64_t torb_process_run_feeding(torb_text command, torb_list arguments, torb_text input, torb_text *output, torb_text *failure);
/* Process.runInheriting */
int64_t torb_process_run_inheriting(torb_text command, torb_list arguments, torb_text *failure);
/* readLine */
bool torb_read_line(torb_text *out);
/* Float64.round */
double torb_round_f64(double value);
/* HashSet.insert, TrieSet.insert */
void torb_set_add(torb_set *set, const void *item);
/* HashSet.clear, TrieSet.clear */
void torb_set_clear(torb_set *set);
/* HashSet.contains, TrieSet.contains */
bool torb_set_contains(torb_set set, const void *item);
/* HashSet.itemAfter, TrieSet.itemAfter */
bool torb_set_item_after(torb_set set, int64_t *cursor, void *item);
/* HashSet.length, TrieSet.length */
int64_t torb_set_length(torb_set set);
/* HashSet.remove, TrieSet.remove */
bool torb_set_remove(torb_set *set, const void *item);
/* Bool.show */
torb_text torb_show_bool(bool value);
/* Char.show */
torb_text torb_show_char(torb_char character);
/* Char.showNested */
torb_text torb_show_char_nested(torb_char character);
/* Float64.show */
torb_text torb_show_f64(double value);
/* Int16.show, Int32.show, Int64.show, Int8.show */
torb_text torb_show_i64(int64_t value);
/* UInt16.show, UInt32.show, UInt64.show, UInt8.show */
torb_text torb_show_u64(uint64_t value);
/* Void.show */
torb_text torb_show_void(torb_void value);
/* sleep */
torb_task *torb_sleep(double seconds);
/* Float64.squareRoot */
double torb_square_root_f64(double value);
/* cancelTask */
void torb_task_cancel(torb_task *self);
/* completedWithin */
torb_task *torb_task_completed_within(torb_task *self, int64_t limit);
/* Task.finished */
bool torb_task_result(torb_task *task, void *out);
/* test */
void torb_test_case(torb_text name, torb_closure body);
/* group */
void torb_test_group(torb_text name, torb_closure body);
/* String.byteAt */
bool torb_text_byte_at(torb_text text, int64_t offset, uint8_t *out);
/* String.charAt */
bool torb_text_char_at(torb_text text, int64_t offset, torb_char *out);
/* String.contains */
bool torb_text_contains(torb_text text, torb_text part);
/* String.endsWith */
bool torb_text_ends_with(torb_text text, torb_text suffix);
/* String.hash */
uint64_t torb_text_hash(torb_text text);
/* String.indexOf */
bool torb_text_index_of(torb_text text, torb_text part, int64_t *out);
/* String.isEmpty */
bool torb_text_is_empty(torb_text text);
/* String.lastIndexOf */
bool torb_text_last_index_of(torb_text text, torb_text part, int64_t *out);
/* String.repeat */
torb_text torb_text_repeat(torb_text text, int64_t times, torb_location at);
/* String.replace */
torb_text torb_text_replace(torb_text text, torb_text part, torb_text replacement);
/* String.showNested */
torb_text torb_text_show_nested(torb_text text);
/* String.sliceBytes */
torb_text torb_text_slice(torb_text text, int64_t from, int64_t to, torb_location at);
/* String.split */
torb_list torb_text_split(torb_text text, torb_text separator);
/* String.startsWith */
bool torb_text_starts_with(torb_text text, torb_text prefix);
/* String.toLowerCase */
torb_text torb_text_to_lower_case(torb_text text);
/* String.toUpperCase */
torb_text torb_text_to_upper_case(torb_text text);
/* String.trim */
torb_text torb_text_trim(torb_text text);
/* tlsClient */
int64_t torb_tls_client(torb_text server_name, torb_text trusted);
/* tlsClose */
void torb_tls_close(int64_t handle);
/* tlsErrorText */
torb_text torb_tls_error_text(int64_t failure);
/* tlsFeed */
void torb_tls_feed(int64_t session, torb_list bytes);
/* tlsHandshake */
int64_t torb_tls_handshake(int64_t session);
/* tlsIdentity */
int64_t torb_tls_identity(torb_text certificates, torb_text private_key);
/* tlsNotifyClose */
void torb_tls_notify_close(int64_t session);
/* tlsProtocol */
torb_text torb_tls_protocol(int64_t session);
/* tlsRead */
int64_t torb_tls_read(int64_t session, torb_list *into, int64_t maximum);
/* tlsServer */
int64_t torb_tls_server(int64_t identity);
/* tlsTakeOutgoing */
void torb_tls_take_outgoing(int64_t session, torb_list *into);
/* tlsWrite */
int64_t torb_tls_write(int64_t session, torb_list bytes, int64_t from);
/* Workers.blocking */
int64_t torb_workers_blocking(void);
/* Workers.count */
int64_t torb_workers_count(void);

#endif /* TORB_NATIVES_H */
