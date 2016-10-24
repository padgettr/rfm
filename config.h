/* Config file for rfm
 */

/*#define RFM_ICON_THEME "Tango"*/
/*#define RFM_ICON_THEME "oxygen"*/
/*#define RFM_ICON_PREFIX "gnome-mime"*/
#define RFM_TOOL_SIZE 22
#define RFM_ICON_SIZE 48
#define RFM_THUMBNAIL_SIZE 128 /* Maximum size for thumb dir normal is 128 */
/*#define RFM_SEND_DND_HOST_NAMES*/
#define RFM_MX_MSGBOX_CHARS 1500 /* Maximum chars for RFM_EXEC_SYNC_MARKUP output; messages exceeding this will be displayed using RFM_EXEC_SYNC_TEXT_BOX mode */
#define RFM_MX_ARGS 128 /* Maximum allowed number of command line arguments in action commands below */
#define RFM_SHOW_WINDOW_DECORATIONS
#define RFM_MOUNT_MEDIA_PATH "/run/media" /* Where specified mount handler mounts filesystems (e.g. udisksctl mount) */

static const time_t parent_mtime_offset=60;   /* Display modified files as bold text (age in seconds) */
static int do_thumbs=1;			/* Attempt to generate thumbnail images of files */
static int showMimeType=0;		/* Display detected mime type on stdout when a file is double-clicked: toggled via -i option */

/* User supplied thumbnailers
 * Enter sub type as "anything" to use thumbnailer for all sub types of mime root
 * The thumbnailer will be called as:
 * $0 /path/to/source/file /path/to/thumbnail thumbnail_size
 */
static const RFM_Thumbnailers thumbnailers[] = {
   /* mime root      mime sub type      thumbnail program */
   { "application",   "dicom",            "/usr/local/bin/mk_dcm_thumb.sh"},
};

/* Built in commands - MUST be present */
static const char *f_rm[]   = { "/bin/rm", "-r", "-f", NULL };
static const char *f_cp[]   = { "/bin/cp", "-p", "-R", "-f", NULL };
static const char *f_mv[]   = { "/bin/mv", "-f", NULL };

/* Run action commands: called as run_action <list of paths to selected files> */
static const char *omxplayer[]  = { "/usr/bin/xterm", "-class", "Dialog", "-geometry", "80x5", "-e", "/usr/bin/omxplayer", "--win", "448,252,1472,828", "--aspect-mode", "Letterbox", NULL };
static const char *play_video[] = { "/usr/bin/xterm", "-class", "Dialog", "-geometry", "80x15", "-e", "/usr/bin/ffplay", "-hide_banner", "-ac", "2", NULL };
static const char *play_audio[] = { "/usr/bin/xterm", "-class", "Dialog", "-geometry", "80x12", "-e", "/usr/bin/play", NULL };
static const char *av_info[]    = { "/usr/bin/mediainfo", "-f", NULL };
static const char *textEdit[]   = { "/usr/local/bin/nedit", NULL };
static const char *pdfView[]    = { "/usr/bin/mupdf", NULL };
static const char *showImage[]  = { "/usr/bin/display", NULL };
static const char *feh[]        = { "/usr/bin/feh", "-F", NULL };
static const char *abiword[]    = { "/usr/bin/abiword", NULL };
static const char *gnumeric[]   = { "/usr/bin/gnumeric", NULL };
static const char *extract_archive[] = { "/usr/local/bin/extractArchive.sh", NULL };
static const char *create_archive[]  = { "/usr/local/bin/createArchive.sh", NULL };
static const char *dcm_dump[] = { "/usr/local/bin/dcmdump.sh", NULL };
static const char *metaflac[] = { "/usr/bin/metaflac", "--list", "--block-type=VORBIS_COMMENT", NULL };
static const char *du[]       = { "/usr/bin/du", "-s", "-h", NULL };
static const char *mount[]    = { "/usr/local/bin/suMount.sh", NULL };
static const char *umount[]   = { "/usr/local/bin/suMount.sh", "-u", NULL };
static const char *udisks_mount[]   = { "/usr/bin/udisksctl", "mount", "--no-user-interaction", "-b", NULL };
static const char *udisks_unmount[] = { "/usr/bin/udisksctl", "unmount", "--no-user-interaction", "-b", NULL };
static const char *properties[]     = { "/usr/local/bin/properties.sh", NULL };
static const char *www[]        = { "/usr/bin/xterm", "-e", "lynx", NULL };
static const char *man[]        = { "/usr/bin/xterm", "-e", "man", "--local-file", NULL };
static const char *java[]       = { "/usr/bin/java", "-jar", NULL };
static const char *audioSpect[] = { "/usr/local/bin/spectrogram.sh", NULL };
static const char *imageInfo[]  = { "/usr/bin/identify", "-verbose", NULL };
static const char *open_with[]  = { "/usr/local/bin/open_with_dmenu.sh", NULL };

/* Tool button commands */
static const char *term_cmd[]  = { "/usr/bin/xterm", "+ls", NULL };
static const char *new_rfm[]  = { "/usr/local/bin/rfm", "-c", NULL };
/* Run actions
 * NOTES: The first three MUST be the built in commands for cp, mv and rm, respectively.
 *        The first matched mime type will become the default action for that kind of file
 *        (i.e. double click opens)
 *        If the mime root is "anything", the action will be shown for all files, but will
 *        NEVER be the default action for any file type.
 *        If the mime sub type is "anything", the action will be shown for all files of type
 *        mime root. It may be the default action if no prior action is defined for the file.
 *        If a mime sub type string is prefixed with '/', then the following string will be
 *        matched within the file's mime sub type. Only applies to mime sub type strings!
 *        All stderr is sent to the filer's stderr.
 *        Any stdout may be displayed by defining the run option.
 * Run options are:
 *   RFM_EXEC_NONE   - run and forget; program is expected to show its own output
 *   RFM_EXEC_TEXT   - run and show output in a fixed font, scrolled text window
 *   RFM_EXEC_PANGO  - run and show output in a dialog window supporting pango markup
 *   RFM_EXEC_PLAIN  - run and show output in a plain text dialog window
 *   RFM_EXEC_INTERNAL - same as RFM_EXEC_PLAIN; intended for required commands cp, mv, rm
 *   RFM_EXEC_MOUNT  - same as RFM_EXEC_PLAIN; this should be specified for the mount command.
 *                     causes the filer to auto switch on success to the directory defined by
 *                     #define RFM_MOUNT_MEDIA_PATH.
 */
static RFM_RunActions run_actions[] = {
   /* name           mime root        mime sub type             argument          run options */
   { "Copy",         "anything",      "anything",               f_cp,             RFM_EXEC_INTERNAL },
   { "Move",         "anything",      "anything",               f_mv,             RFM_EXEC_INTERNAL },
   { "Delete",       "anything",      "anything",               f_rm,             RFM_EXEC_INTERNAL },
   { "Properties",   "anything",      "anything",               properties,       RFM_EXEC_PANGO },
   { "Open with...", "anything",      "anything",               open_with,        RFM_EXEC_NONE },
   { "feh",          "image",         "jpeg",                   feh,              RFM_EXEC_NONE },
   { "feh",          "image",         "png",                    feh,              RFM_EXEC_NONE },
   { "Open image",   "image",         "anything",               showImage,        RFM_EXEC_NONE },
   { "info",         "image",         "anything",               imageInfo,        RFM_EXEC_TEXT },
   { "abiword",      "application",   "vnd.oasis.opendocument.text",          abiword,   RFM_EXEC_NONE },
   { "gnumeric",     "application",   "vnd.oasis.opendocument.spreadsheet",   gnumeric,  RFM_EXEC_NONE },
   { "abiword",      "application",   "vnd.openxmlformats-officedocument.wordprocessingml.document", abiword, RFM_EXEC_NONE },
   { "gnumeric",     "application",   "vnd.openxmlformats-officedocument.spreadsheetml.sheet",      gnumeric, RFM_EXEC_NONE },
   { "abiword",      "application",   "msword",                 abiword,          RFM_EXEC_NONE },
   { "abiword",      "application",   "x-abiword",              abiword,          RFM_EXEC_NONE },
   { "gnumeric",     "application",   "x-gnumeric",             gnumeric,         RFM_EXEC_NONE },
   { "gnumeric",     "application",   "/vnd.ms-excel",          gnumeric,         RFM_EXEC_NONE },
   { "extract",      "application",   "x-compressed-tar",       extract_archive,  RFM_EXEC_NONE },
   { "extract",      "application",   "x-bzip-compressed-tar",  extract_archive,  RFM_EXEC_NONE },
   { "extract",      "application",   "x-xz-compressed-tar",    extract_archive,  RFM_EXEC_NONE },
   { "extract",      "application",   "zip",                    extract_archive,  RFM_EXEC_NONE },
   { "extract",      "application",   "x-rpm",                  extract_archive,  RFM_EXEC_NONE },
   { "view",         "application",   "dicom",                  showImage,        RFM_EXEC_NONE },
   { "dcm info",     "application",   "dicom",                  dcm_dump,         RFM_EXEC_TEXT },
   { "Open",         "application",   "pdf",                    pdfView,          RFM_EXEC_NONE },
   { "View",         "application",   "epub+zip",               extract_archive,  RFM_EXEC_NONE },
   { "View",         "application",   "x-troff-man",            man,              RFM_EXEC_NONE },
   { "Run",          "application",   "x-java-archive",         java,             RFM_EXEC_NONE },
   { "Open as text", "application",   "anything",               textEdit,         RFM_EXEC_NONE },
   { "Play",         "audio",         "anything",               play_audio,       RFM_EXEC_NONE },
   { "flac info",    "audio",         "flac",                   metaflac,         RFM_EXEC_PLAIN },
   { "info",         "audio",         "anything",               av_info,          RFM_EXEC_PLAIN },
   { "stats",        "audio",         "anything",               audioSpect,       RFM_EXEC_PANGO },
   { "count",        "inode",         "directory",              du,               RFM_EXEC_PLAIN },
   { "archive",      "inode",         "directory",              create_archive,   RFM_EXEC_NONE },
   { "mount",        "inode",         "mount-point",            mount,            RFM_EXEC_PLAIN },
   { "unmount",      "inode",         "mount-point",            umount,           RFM_EXEC_PLAIN },
   { "mount",        "inode",         "blockdevice",            udisks_mount,     RFM_EXEC_MOUNT },
   { "unmount",      "inode",         "blockdevice",            udisks_unmount,   RFM_EXEC_PLAIN },
   { "edit",         "text",          "anything",               textEdit,         RFM_EXEC_NONE },
   { "Open",         "text",          "html",                   www,              RFM_EXEC_NONE },
   { "Play",         "video",         "anything",               play_video,       RFM_EXEC_NONE },
   { "omxplayer",    "video",         "anything",               omxplayer,        RFM_EXEC_NONE },
   { "info",         "video",         "anything",               av_info,          RFM_EXEC_TEXT },
};

/* Toolbar button definitions
 * icon is the name (not including .png) of an icon in the current theme.
 * function is the name of a function to be defined here. If function is NULL,
 * the argument is assumed to be a shell command. All commands are run
 * async - no stdout / stderr is shown. The current working directory for
 * the command environment is changed to the currently displayed directory.
 * If function is not NULL, arguments may be passed as a const char *arg[].
 */

static const char *dev_disk_path[]= { "/dev/disk" };
static void show_disk_devices(const char **path) {
	char *parent_dir=strdup(path[0]);
	set_parent_path(parent_dir);
	free(parent_dir);
}

static RFM_ToolButtons tool_buttons[] = {
   /* name           icon                       function    		argument*/
   { "xterm",        "utilities-terminal",      NULL,       		 term_cmd },
   { "rfm",          "system-file-manager",     NULL,       		 new_rfm },
   { "mounts",       "drive-harddisk",     	   show_disk_devices, dev_disk_path },
};
