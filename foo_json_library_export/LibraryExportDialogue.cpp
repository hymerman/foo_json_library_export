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

		// JSON will be formatted as such:
		// [{"path":"path/to/1", "title":"abc"},{"path":"path/to/2", "title":"def"}]

		console::print("Creating in-memory JSON.");

		rapidjson::Document document;
		document.SetArray();

		auto& allocator = document.GetAllocator();

		document.Reserve(library.get_count(), allocator);

		library.enumerate([&document, &allocator](const metadb_handle_ptr& track) {
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
