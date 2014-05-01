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

} // anonymous namespace

namespace libraryexport
{

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
		// todo: store this value between dialogue invocations.
		SetDlgItemText(IDC_FILE_PATH_TEXT, _T(""));
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
		// todo: do this all asynchronously with a nice progress bar or something.
		console::print("Starting library export.");

		pfc::string8 filePath;
		uGetDlgItemText(*this, IDC_FILE_PATH_TEXT, filePath);

		console::printf("Chosen path: %s", filePath.get_ptr());

		pfc::list_t<metadb_handle_ptr> library;
		static_api_ptr_t<library_manager> lm;
		lm->get_all_items(library);

		// Lock the database for the duration of this scope.
		DatabaseScopeLock databaseLock;

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

		document.Reserve(library.get_count(), allocator);

		library.enumerate([&](const metadb_handle_ptr& track) {
			const file_info* fileInfo = nullptr;
			const bool success = track->get_info_locked(fileInfo);

			if(!success)
			{
				console::printf("Failed to get info on track: %s", track->get_path());
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
		});

		console::print("JSON built up in memory; opening file to save.");

		// todo: error checking.
		FILE* file = fopen(filePath.get_ptr(), "w");

		if(!file)
		{
			console::print("Failed to open file for writing; aborting");
			return;
		}

		static const size_t fileWriteBufferSize = 2048;
		char fileWriteBuffer[fileWriteBufferSize];
		rapidjson::FileWriteStream fileStream(file, fileWriteBuffer, fileWriteBufferSize);
		// todo: add UI option for pretty print.
		//rapidjson::Writer<rapidjson::FileWriteStream> writer(fileStream);
		rapidjson::PrettyWriter<rapidjson::FileWriteStream> writer(fileStream);

		console::print("File stream open. Writing JSON.");

		document.Accept(writer);

		// todo: error checking.
		const int fclose_status = fclose(file);

		if(fclose_status != 0)
		{
			console::print("Error writing file.");
		}
		else
		{
			console::print("File written successfully.");
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
	catch(std::exception const & e)
	{
		popup_message::g_complain("Dialog creation failure", e);
	}
}

} // namespace libraryexport
