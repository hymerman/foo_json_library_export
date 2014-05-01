#include "LibraryExportDialogue.h"

#include "FoobarSDKWrapper.h"
#include "ATLHelpersWrapper.h"
#include "DatabaseScopeLock.h"
#include "LibraryExport.h"
#include "RapidJsonWrapper.h"
#include "resource.h"
#include "ToString.h"

#include <memory>
#include <regex>

namespace
{

pfc::string8 title_format(const metadb_handle_ptr& track, const file_info& fileInfo, const titleformat_object::ptr& script)
{
	pfc::string8 formatted;
	track->format_title_from_external_info_nonlocking(fileInfo, nullptr, formatted, script, nullptr);
	return formatted;
}

// string_key must exist until after the JSON object is destroyed, as a copy will not be taken.
void add_string_value_to_json_object_if_not_empty(const char* string_key, const pfc::string8& string_value, rapidjson::Value& json_object, rapidjson::MemoryPoolAllocator<rapidjson::CrtAllocator>& allocator)
{
	if(!string_value.is_empty())
	{
		rapidjson::Value json_value(string_value.get_ptr(), allocator);
		json_object.AddMember(string_key, json_value, allocator);
	}
}

FILE* fopen_or_exception(const char* fileName, const char* mode)
{
	if(!fileName || !mode)
	{
		throw std::invalid_argument("Invalid argument passed to fopen");
	}

#pragma warning (push)
#pragma warning (disable: 4996)
	return fopen(fileName, mode);
#pragma warning (pop)
}

// {744A7590-0DE7-4429-B31E-9B30FDBE2545}
static const GUID config_export_path_guid = { 0x744a7590, 0xde7, 0x4429, { 0xb3, 0x1e, 0x9b, 0x30, 0xfd, 0xbe, 0x25, 0x45 } };
cfg_string config_export_path(config_export_path_guid, "");

} // anonymous namespace

namespace libraryexport
{

class library_export_process : public threaded_process_callback
{
public:
	explicit library_export_process(const pfc::string8& filePath, const pfc::list_t<metadb_handle_ptr>& library)
	    : m_filePath(filePath)
		, m_failureMessage()
		, m_library(library)
	{
	}

	void on_init(HWND)
	{
	}

	void run(threaded_process_status& p_status, abort_callback& p_abort)
	{
		try
		{
			// Start the status off at 0%.
			p_status.set_progress(0, 1);

			// Open the file for writing before doing anything else (to avoid wasting time in case it's not writable).
			console::print("Opening output file.");
			std::shared_ptr<FILE> file = std::shared_ptr<FILE>(fopen_or_exception(m_filePath.get_ptr(), "w"), [](FILE* file){ if(file) fclose(file); });

			if(!file)
			{
				m_failureMessage = "Failed to open file for writing; aborting";
				console::print(m_failureMessage);
				return;
			}

			// Compile titleformatting scripts ahead of time.
			console::print("Compiling titleformatting scripts ahead of time.");

			static_api_ptr_t<titleformat_compiler> compiler;

			// Playback statistics fields.
			titleformat_object::ptr first_played_script;
			compiler->compile_force(first_played_script, "[%first_played%]");
			titleformat_object::ptr last_played_script;
			compiler->compile_force(last_played_script, "[%last_played%]");
			titleformat_object::ptr play_count_script;
			compiler->compile_force(play_count_script, "[%play_count%]");
			titleformat_object::ptr added_script;
			compiler->compile_force(added_script, "[%added%]");
			titleformat_object::ptr rating_script;
			compiler->compile_force(rating_script, "[%rating%]");

			// foo_customdb fields when using marc2003's last.fm sync scripts.
			titleformat_object::ptr lastfm_playcount_script;
			compiler->compile_force(lastfm_playcount_script, "[%LASTFM_PLAYCOUNT_DB%]");
			titleformat_object::ptr lastfm_loved_script;
			compiler->compile_force(lastfm_loved_script, "[%LASTFM_LOVED_DB%]");

			// todo: allow user to specify list of extra titleformatting snippets they wish to be saved.

			console::print("Creating in-memory JSON.");

			// JSON will be formatted as such:
			// [{"path":"path/to/1", "title":"abc"},{"path":"path/to/2", "title":"def"}]
			rapidjson::Document document;
			document.SetArray();

			auto& allocator = document.GetAllocator();

			// Lock the database for the duration of this scope.
			DatabaseScopeLock databaseLock;

			document.Reserve(m_library.get_count(), allocator);

			for(t_size track_index = 0; track_index < m_library.get_count(); ++track_index)
			{
				// Check if the user has chosen to abort; will throw an exception if this is the case.
				p_abort.check();

				// Update the progress bar.
				p_status.set_progress(track_index, m_library.get_count());

				const metadb_handle_ptr& track = m_library.get_item(track_index);

				const file_info* fileInfo = nullptr;
				const bool success = track->get_info_locked(fileInfo);

				if(!success || !fileInfo)
				{
					uPrintf(m_failureMessage, "Failed to get info on track: %s", track->get_path());
					console::print(m_failureMessage);
					return;
				}

				// Create a JSON object for the track.
				rapidjson::Value trackValue;
				trackValue.SetObject();

				// Track properties.

				// No need to copy string (by passing rapidjson allocator)
				// as foobar guarantees string is valid until metadb handle is released,
				// which will be after we've saved the file.
				// In general though, most strings below will need to be copied as the API doesn't guarantee they'll stick around.
				rapidjson::Value pathValue(track->get_path());
				trackValue.AddMember("path", pathValue, allocator);

				rapidjson::Value subsongIndexValue(track->get_subsong_index());
				trackValue.AddMember("subsong_index", subsongIndexValue, allocator);

				// Let's not bother saving out timestamp and file size;
				// these are properties of the files themselves which require no special parsing or decoding.

				// This is the only thing the file_info struct has that isn't calculated from other fields.
				// e.g. "number of samples" which the foobar interface shows in a track's properties,
				// is actually calculated from length and bitrate.
				rapidjson::Value lengthValue(fileInfo->get_length());
				trackValue.AddMember("length", lengthValue, allocator);

				// Replaygain data.
				const auto& replay_gain_info = fileInfo->get_replaygain();
				const bool is_album_gain_present = replay_gain_info.is_album_gain_present();
				const bool is_album_peak_present = replay_gain_info.is_album_peak_present();
				const bool is_track_gain_present = replay_gain_info.is_track_gain_present();
				const bool is_track_peak_present = replay_gain_info.is_track_peak_present();

				if(is_album_gain_present || is_album_peak_present || is_track_gain_present || is_track_peak_present)
				{
					rapidjson::Value replayGainContainerValue;
					replayGainContainerValue.SetObject();

					if(replay_gain_info.is_album_gain_present())
					{
						rapidjson::Value replayGainValue(replay_gain_info.m_album_gain);
						replayGainContainerValue.AddMember("album_gain", replayGainValue, allocator);
					}

					if(replay_gain_info.is_album_peak_present())
					{
						rapidjson::Value replayGainValue(replay_gain_info.m_album_peak);
						replayGainContainerValue.AddMember("album_peak", replayGainValue, allocator);
					}

					if(replay_gain_info.is_track_gain_present())
					{
						rapidjson::Value replayGainValue(replay_gain_info.m_track_gain);
						replayGainContainerValue.AddMember("track_gain", replayGainValue, allocator);
					}

					if(replay_gain_info.is_track_peak_present())
					{
						rapidjson::Value replayGainValue(replay_gain_info.m_track_peak);
						replayGainContainerValue.AddMember("track_peak", replayGainValue, allocator);
					}

					trackValue.AddMember("replaygain", replayGainContainerValue, allocator);
				}

				// 'info', which is technical details about the file.
				if(fileInfo->info_get_count() > 0)
				{
					rapidjson::Value infoValue;
					infoValue.SetObject();

					for(t_size i = 0; i < fileInfo->info_get_count(); ++i)
					{
						rapidjson::Value individualInfoValue(fileInfo->info_enum_value(i), allocator);
						infoValue.AddMember(fileInfo->info_enum_name(i), allocator, individualInfoValue, allocator);
					}

					trackValue.AddMember("info", infoValue, allocator);
				}

				// 'meta', which are metadata about the track, normally called its 'tags'.
				// Note that unlike 'info', all meta fields can be multi-valued, so we save them out as arrays.
				// todo: add option to save single-valued fields as values directly rather than one-element arrays.
				if(fileInfo->meta_get_count() > 0)
				{
					rapidjson::Value metaValue;
					metaValue.SetObject();

					for(t_size i = 0; i < fileInfo->meta_get_count(); ++i)
					{
						rapidjson::Value individualMetaValue;
						individualMetaValue.SetArray();
						individualMetaValue.Reserve(fileInfo->meta_enum_value_count(i), allocator);

						for(t_size j = 0; j < fileInfo->meta_enum_value_count(i); ++j)
						{
							individualMetaValue.PushBack(fileInfo->meta_enum_value(i, j), allocator);
						}

						metaValue.AddMember(fileInfo->meta_enum_name(i), allocator, individualMetaValue, allocator);
					}

					trackValue.AddMember("meta", metaValue, allocator);
				}

				// Playback statistics. I don't know if there's an API for the component, or if that's even possible,
				// so we do the expensive and inextensible thing and query for its fields using titleformatting.
				// Scripts have already been compiled; we just need to format the track with them and add their values
				// if present.

				const pfc::string8 first_played_string		= title_format(track, *fileInfo, first_played_script);
				const pfc::string8 last_played_string		= title_format(track, *fileInfo, last_played_script);
				const pfc::string8 play_count_string		= title_format(track, *fileInfo, play_count_script);
				const pfc::string8 added_string				= title_format(track, *fileInfo, added_script);
				const pfc::string8 rating_string			= title_format(track, *fileInfo, rating_script);

				const pfc::string8 lastfm_playcount_string	= title_format(track, *fileInfo, lastfm_playcount_script);
				const pfc::string8 lastfm_loved_string		= title_format(track, *fileInfo, lastfm_loved_script);

				if( !first_played_string.is_empty()		||
					!last_played_string.is_empty()		||
					!play_count_string.is_empty()		||
					!added_string.is_empty()			||
					!rating_string.is_empty()			||
					!lastfm_playcount_string.is_empty()	||
					!lastfm_loved_string.is_empty()
				)
				{
					rapidjson::Value playback_stats_value;
					playback_stats_value.SetObject();

					add_string_value_to_json_object_if_not_empty("first_played"	, first_played_string		, playback_stats_value, allocator);
					add_string_value_to_json_object_if_not_empty("last_played"	, last_played_string		, playback_stats_value, allocator);
					add_string_value_to_json_object_if_not_empty("play_count"	, play_count_string			, playback_stats_value, allocator);
					add_string_value_to_json_object_if_not_empty("added"		, added_string				, playback_stats_value, allocator);
					add_string_value_to_json_object_if_not_empty("rating"		, rating_string				, playback_stats_value, allocator);

					add_string_value_to_json_object_if_not_empty("lastfm_playcount"	, lastfm_playcount_string	, playback_stats_value, allocator);
					add_string_value_to_json_object_if_not_empty("lastfm_loved"		, lastfm_loved_string		, playback_stats_value, allocator);

					trackValue.AddMember("playback_stats", playback_stats_value, allocator);
				}

				// Finally, add the whole track object to the document.
				document.PushBack(trackValue, allocator);
			}

			console::print("JSON built up in memory; saving to output file.");

			static const size_t fileWriteBufferSize = 2048;
			char fileWriteBuffer[fileWriteBufferSize];
			rapidjson::FileWriteStream fileStream(file.get(), fileWriteBuffer, fileWriteBufferSize);
			// todo: add UI option for pretty print.
			//rapidjson::Writer<rapidjson::FileWriteStream> writer(fileStream);
			rapidjson::PrettyWriter<rapidjson::FileWriteStream> writer(fileStream);

			console::print("File stream open. Writing JSON.");

			document.Accept(writer);

			console::print("File written successfully.");
		}
		catch(const exception_aborted&)
		{
		}
		catch(const std::exception& e)
		{
			m_failureMessage = "Exception whilst exporting library: ";
			m_failureMessage += pfc::string8(e.what());
		}
		catch(...)
		{
			m_failureMessage = "Unknown exception encountered whilst exporting library";
		}
	}

	void on_done(HWND, bool p_was_aborted)
	{
		if(!p_was_aborted)
		{
			if(!m_failureMessage.is_empty())
			{
				popup_message::g_complain("JSON library export failure", m_failureMessage);
			}
		}
	}

private:
	pfc::string8 m_filePath;
	pfc::string8 m_failureMessage;
	pfc::list_t<metadb_handle_ptr> m_library;
};


class LibraryExportDialogue : public CDialogImpl<LibraryExportDialogue> {
public:
	LibraryExportDialogue()
		: CDialogImpl<LibraryExportDialogue>()
	{}

	enum { IDD = IDD_LIBRARY_EXPORT_DIALOGUE };

	BEGIN_MSG_MAP(LibraryExportDialogue)
		MSG_WM_INITDIALOG(OnInitDialog)
		COMMAND_HANDLER_EX(IDOK, BN_CLICKED, OnOk)
		COMMAND_HANDLER_EX(IDCANCEL, BN_CLICKED, OnCancel)
	END_MSG_MAP()

private:

	BOOL OnInitDialog(CWindow, LPARAM)
	{
		uSetDlgItemText(*this, IDC_FILE_PATH_TEXT, config_export_path);
		ShowWindowCentered(*this, GetParent()); // Function declared in SDK helpers.
		return TRUE;
	}

	void OnOk(UINT, int, CWindow)
	{
		exportLibrary();

		DestroyWindow();
	}

	void exportLibrary()
	{
		console::print("Starting library export.");

		pfc::string8 filePath;
		uGetDlgItemText(*this, IDC_FILE_PATH_TEXT, filePath);
		config_export_path = filePath;

		console::printf("Chosen path: %s", filePath.get_ptr());

		pfc::list_t<metadb_handle_ptr> library;
		static_api_ptr_t<library_manager> lm;
		lm->get_all_items(library);

		try
		{
			service_ptr_t<threaded_process_callback> cb = new service_impl_t<library_export_process>(filePath, library);
			static_api_ptr_t<threaded_process>()->run_modeless(
			    cb,
			    threaded_process::flag_show_progress | threaded_process::flag_show_abort,
			    core_api::get_main_window(),
			    "JSON Library export"
			);
		}
		catch(std::exception const& e)
		{
			popup_message::g_complain("Could not start JSON library export process", e);
		}
	}

	void OnCancel(UINT, int, CWindow)
	{
		DestroyWindow();
	}
};

void showLibraryExportDialogue()
{
	try
	{
		// ImplementModelessTracking registers our dialog to receive dialog messages thru main app loop's IsDialogMessage().
		// CWindowAutoLifetime creates the window in the constructor (taking the parent window as a parameter) and deletes the object when the window has been destroyed (through WTL's OnFinalMessage).
		new CWindowAutoLifetime<ImplementModelessTracking<LibraryExportDialogue>>(core_api::get_main_window());
	}
	catch(std::exception const& e)
	{
		popup_message::g_complain("Dialog creation failure", e);
	}
}

} // namespace libraryexport
