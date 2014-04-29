#include "FoobarSDKWrapper.h"

#include "LibraryExportDialogue.h"

// {1E5E0CCD-AE63-45FA-A581-930DBB954A06}
static const GUID g_mainmenu_group_id = { 0x1e5e0ccd, 0xae63, 0x45fa, { 0xa5, 0x81, 0x93, 0xd, 0xbb, 0x95, 0x4a, 0x6 } };

static mainmenu_group_popup_factory g_mainmenu_group(
	g_mainmenu_group_id,
	mainmenu_groups::file,
	static_cast<t_uint32>(mainmenu_commands::sort_priority_dontcare),
	"foo_json_library_export"
);

namespace libraryexport {

class FooJSONLibraryExportMainMenu : public mainmenu_commands
{
public:
	enum CommandId
	{
		LibraryExportDialogueCommand = 0,
		NumCommands
	};

	virtual t_uint32 get_command_count() override
	{
		return NumCommands;
	}

	virtual GUID get_command(t_uint32 p_index) override
	{
		// {6186B0BE-3BEE-4FC0-BD8D-64BBABF85F46}
		static const GUID guid_library_export_dialogue = { 0x6186b0be, 0x3bee, 0x4fc0, { 0xbd, 0x8d, 0x64, 0xbb, 0xab, 0xf8, 0x5f, 0x46 } };

		switch(p_index)
		{
			case LibraryExportDialogueCommand:
				return guid_library_export_dialogue;
			default:
				uBugCheck(); // should never happen unless somebody called us with invalid parameters - bail
		}
	}

	virtual void get_name(t_uint32 p_index, pfc::string_base& p_out) override
	{
		switch(p_index)
		{
			case LibraryExportDialogueCommand:
				p_out = "JSON library export...";
				break;
			default:
				uBugCheck(); // should never happen unless somebody called us with invalid parameters - bail
		}
	}

	virtual bool get_description(t_uint32 p_index, pfc::string_base& p_out) override
	{
		switch(p_index)
		{
			case LibraryExportDialogueCommand:
				p_out = "Exports the music library as a JSON file.";
				return true;
			default:
				uBugCheck(); // should never happen unless somebody called us with invalid parameters - bail
		}
	}

	virtual GUID get_parent() override
	{
		return g_mainmenu_group_id;
	}

	virtual void execute(t_uint32 p_index, service_ptr_t<service_base> p_callback) override
	{
		switch(p_index)
		{
			case LibraryExportDialogueCommand:
				showLibraryExportDialogue();
				break;
			default:
				uBugCheck(); // should never happen unless somebody called us with invalid parameters - bail
		}
	}
};

static mainmenu_commands_factory_t<FooJSONLibraryExportMainMenu> g_FooJSONLibraryExportMainMenu_factory;

} // namespace libraryexport
