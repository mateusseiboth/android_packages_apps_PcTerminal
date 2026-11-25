/* The container's apps: .desktop entries, their command lines and their icon files. */
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include "check.h"
#include "desktop.h"

static void splits_exec_lines_and_drops_field_codes(void)
{
	struct app_argv a;
	CHECK(!split_exec("gnome-text-editor %U", &a));
	CHECK(a.argc == 1);
	CHECK_STR(a.argv[0], "gnome-text-editor");
	CHECK(!split_exec("env  FOO=1 \"/opt/My App/run\" --name=\"a \\\"b\\\"\" %f 100%% %i %c %k", &a));
	CHECK(a.argc == 5);
	CHECK_STR(a.argv[0], "env");
	CHECK_STR(a.argv[1], "FOO=1");
	CHECK_STR(a.argv[2], "/opt/My App/run");
	CHECK_STR(a.argv[3], "--name=a \"b\"");
	CHECK_STR(a.argv[4], "100%");
	CHECK(!split_exec("sh -c \"echo \\\\$HOME\"", &a));
	CHECK_STR(a.argv[2], "echo \\$HOME");
	CHECK(split_exec("\"unterminated", &a));
	CHECK(split_exec("", &a));
}

static void reads_the_desktop_entry_group(void)
{
	struct desktop_app app;
	const char *text = "# comment\n[Desktop Entry]\nType=Application\nName=Text Editor\nName[pt_BR]=Editor de texto\n"
			   "Name[pt]=Editor\nExec=gnome-text-editor %U\nIcon=org.gnome.TextEditor\nTerminal=false\n"
			   "Categories=GNOME;GTK;Utility;\n\n[Desktop Action new-window]\nName=New Window\nExec=other\n";
	CHECK(!parse_desktop_entry(text, "pt_BR", &app));
	CHECK_STR(app.name, "Editor de texto");
	CHECK_STR(app.exec, "gnome-text-editor %U");
	CHECK_STR(app.icon, "org.gnome.TextEditor");
	CHECK(!app.terminal);
	CHECK_STR(app.categories, "GNOME;GTK;Utility;");
	CHECK(!parse_desktop_entry(text, "pt_PT", &app));
	CHECK_STR(app.name, "Editor");
	CHECK(!parse_desktop_entry(text, NULL, &app));
	CHECK_STR(app.name, "Text Editor");
	CHECK(!parse_desktop_entry("[Desktop Entry]\nType=Application\nName=Top\\sTool\nExec=htop\nTerminal=true\n", NULL, &app));
	CHECK_STR(app.name, "Top Tool");
	CHECK(app.terminal);
}

static void skips_hidden_and_non_applications(void)
{
	struct desktop_app app;
	CHECK(parse_desktop_entry("[Desktop Entry]\nType=Application\nName=A\nExec=a\nNoDisplay=true\n", NULL, &app));
	CHECK(parse_desktop_entry("[Desktop Entry]\nType=Application\nName=A\nExec=a\nHidden=true\n", NULL, &app));
	CHECK(parse_desktop_entry("[Desktop Entry]\nType=Link\nName=A\nURL=http://x\n", NULL, &app));
	CHECK(parse_desktop_entry("[Desktop Entry]\nType=Application\nName=A\n", NULL, &app)); /* no Exec */
	CHECK(parse_desktop_entry("[Other]\nType=Application\nName=A\nExec=a\n", NULL, &app));
}

static char dir[64];

static int make_rootfs(void)
{
	char cmd[2048];
	snprintf(dir, sizeof dir, "/tmp/pccapps-XXXXXX");
	CHECK(mkdtemp(dir) != NULL);
	snprintf(cmd, sizeof cmd,
		 "cd %s && mkdir -p host/usr/share/icons/hicolor/256x256/apps r/usr/share/applications r/usr/share/pixmaps "
		 "r/usr/share/icons/hicolor/48x48/apps r/usr/share/icons/hicolor/128x128/apps "
		 "r/usr/share/icons/hicolor/scalable/apps r/usr/local/share/applications && "
		 "printf '[Desktop Entry]\\nType=Application\\nName=Editor\\nExec=editor %%%%F\\nIcon=editor\\n' > r/usr/share/applications/org.x.Editor.desktop && "
		 "printf '[Desktop Entry]\\nType=Application\\nName=Vector\\nExec=vector\\nIcon=vector\\n' > r/usr/share/applications/vector.desktop && "
		 "printf '[Desktop Entry]\\nType=Application\\nName=Old\\nExec=old\\nIcon=old.xpm\\n' > r/usr/share/applications/old.desktop && "
		 "printf '[Desktop Entry]\\nType=Application\\nName=Tiny\\nExec=tiny\\nIcon=tiny\\n' > r/usr/local/share/applications/tiny.desktop && "
		 "printf '[Desktop Entry]\\nType=Application\\nName=Leak\\nExec=leak\\nIcon=leak\\n' > r/usr/share/applications/leak.desktop && "
		 "printf '[Desktop Entry]\\nType=Application\\nName=Hidden\\nExec=h\\nNoDisplay=true\\n' > r/usr/share/applications/hidden.desktop && "
		 "printf '[Desktop Entry]\\nType=Application\\nName=Abs\\nExec=abs\\nIcon=/opt/abs/icon.png\\n' > r/usr/share/applications/abs.desktop && "
		 "mkdir -p r/opt/abs && echo png > r/opt/abs/icon.png && "
		 "echo png > r/usr/share/icons/hicolor/128x128/apps/editor.png && "
		 "echo png > r/usr/share/icons/hicolor/48x48/apps/editor.png && "
		 "echo svg > r/usr/share/icons/hicolor/scalable/apps/editor.svg && "
		 "echo svg > r/usr/share/icons/hicolor/scalable/apps/vector.svg && "
		 "echo png > r/usr/share/icons/hicolor/48x48/apps/vector.png && "
		 "echo xpm > r/usr/share/pixmaps/old.xpm && "
		 "echo png > r/usr/share/icons/hicolor/48x48/apps/tiny.png && "
		 "echo secret > host/usr/share/icons/hicolor/256x256/apps/leak.png && "
		 "ln -s ../../../../../../../host/usr/share/icons/hicolor/256x256/apps/leak.png r/usr/share/icons/hicolor/128x128/apps/leak.png",
		 dir);
	CHECK(!system(cmd));
	snprintf(cmd, sizeof cmd, "%s/r", dir);
	return open(cmd, O_DIRECTORY | O_RDONLY | O_CLOEXEC);
}

static const struct desktop_app *find_app(const struct desktop_app *apps, int n, const char *id)
{
	for (int i = 0; i < n; i++)
		if (!strcmp(apps[i].id, id))
			return &apps[i];
	return NULL;
}

static void lists_apps_with_their_best_icons(void)
{
	int root = make_rootfs();
	struct desktop_app *apps = NULL;
	int n = list_apps(root, NULL, &apps);
	CHECK(n == 6);
	CHECK_STR(apps[0].id, "abs"); /* sorted by id */
	const struct desktop_app *editor = find_app(apps, n, "org.x.Editor");
	CHECK(editor && !strcmp(editor->icon_path, "/usr/share/icons/hicolor/128x128/apps/editor.png"));
	CHECK(editor && !strcmp(editor->icon_format, "png"));
	CHECK(editor && editor->argv.argc == 1 && !strcmp(editor->argv.argv[0], "editor"));
	const struct desktop_app *vector = find_app(apps, n, "vector");
	CHECK(vector && !strcmp(vector->icon_path, "/usr/share/icons/hicolor/scalable/apps/vector.svg"));
	CHECK(vector && !strcmp(vector->icon_format, "svg"));
	const struct desktop_app *old = find_app(apps, n, "old");
	CHECK(old && !strcmp(old->icon_path, "/usr/share/pixmaps/old.xpm"));
	const struct desktop_app *tiny = find_app(apps, n, "tiny");
	CHECK(tiny && !strcmp(tiny->icon_path, "/usr/share/icons/hicolor/48x48/apps/tiny.png"));
	const struct desktop_app *abs = find_app(apps, n, "abs");
	CHECK(abs && !strcmp(abs->icon_path, "/opt/abs/icon.png"));
	const struct desktop_app *leak = find_app(apps, n, "leak"); /* its link climbs out: clamped, so missing */
	CHECK(leak && !*leak->icon_path && leak->icon_format == NULL);
	CHECK(find_app(apps, n, "hidden") == NULL);
	free(apps);

	int size = 0;
	const char *format = NULL;
	int fd = open_icon(root, "/usr/share/icons/hicolor/128x128/apps/editor.png", &format, &size);
	CHECK(fd >= 0 && size == 4 && !strcmp(format, "png"));
	close(fd);
	CHECK(open_icon(root, "/usr/share/icons/hicolor/128x128/apps/leak.png", &format, &size) < 0);
	CHECK(open_icon(root, "/usr/share/applications/old.desktop", &format, &size) < 0); /* not an image */
	CHECK(open_icon(root, "/../host/usr/share/icons/hicolor/256x256/apps/leak.png", &format, &size) < 0);
	close(root);
	char cmd[128];
	snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
	CHECK(!system(cmd));
}

int main(void)
{
	RUN(splits_exec_lines_and_drops_field_codes);
	RUN(reads_the_desktop_entry_group);
	RUN(skips_hidden_and_non_applications);
	RUN(lists_apps_with_their_best_icons);
	return CHECK_DONE();
}
