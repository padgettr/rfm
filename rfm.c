/* RFM - Rod's File Manager
 *
 * See LICENSE file for copyright and license details.
 *
 * Edit config.h to define run actions and set options
 *
 * Compile with: gcc -Wall -g rfm.c `pkg-config --libs --cflags gtk+-3.0` -o rfm
 *            OR use the supplied Makefile.
 */

#include <stdlib.h>
#include <gtk/gtk.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <stdio.h>
#include <mntent.h>
#include <icons.h>

#define PROG_NAME "rfm"
#define DND_ACTION_MASK GDK_ACTION_ASK|GDK_ACTION_COPY|GDK_ACTION_MOVE
#define INOTIFY_MASK IN_MOVE|IN_CREATE|IN_CLOSE_WRITE|IN_DELETE|IN_DELETE_SELF|IN_MOVE_SELF
#define TARGET_URI_LIST 0
#define N_TARGETS 1 /* G_N_ELEMENTS(target_entry) */
#define PIPE_SZ 65535 /* Kernel pipe size */

typedef struct {
   gchar *thumbRoot;
   gchar *thumbSub;
   gchar *thumbCmdName;
} RFM_Thumbnailers;

typedef struct {
   gchar *runName;
   gchar *runRoot;
   gchar *runSub;
   const gchar **runCmdName;
   gint  runOpts;
} RFM_RunActions;

typedef struct {
   gchar *buttonName;
   gchar *buttonIcon;
   void (*func)(const gchar **args);
   const gchar **args;
} RFM_ToolButtons;

typedef struct {
   gchar *name;
   gint  runOpts;
   GPid  pid;
   gint  stdOut_fd;
   gint  stdErr_fd;
   char *stdOut;
   char *stdErr;
   int   status;
} RFM_ChildAttribs;

typedef struct {
   GtkWidget *menu;
   GtkWidget *copy;
   GtkWidget *move;
} RFM_dndMenu;

typedef struct {
   GtkWidget *menu;
   GtkWidget **action;
   GtkWidget *separator[2];
} RFM_fileMenu;

typedef struct {
   gboolean rfm_localDrag;
   gboolean rfm_thumbJobs;
} RFM_flags;

typedef struct {
   GdkPixbuf *file, *dir;
   GdkPixbuf *symlinkDir;
   GdkPixbuf *unmounted;
   GdkPixbuf *mounted;
   GdkPixbuf *symlink;
   GdkPixbuf *up;
   GdkPixbuf *home;
   GdkPixbuf *stop;
   GdkPixbuf *refresh;
   GdkPixbuf *info;
} RFM_defaultPixbufs;

enum {
   COL_PATH,
   COL_DISPLAY_NAME,
   COL_PIXBUF,
   COL_IS_DIRECTORY,
   COL_MIME_ROOT,
   COL_MIME_SUB,
   COL_IS_SYMLINK,
   COL_MTIME,
   NUM_COLS
};

enum {
   RFM_EXEC_NONE,
   RFM_EXEC_TEXT,
   RFM_EXEC_PANGO,
   RFM_EXEC_PLAIN,
   RFM_EXEC_INTERNAL,
   RFM_EXEC_MOUNT
};

static GtkTargetEntry target_entry[] = {
   {"text/uri-list", 0, TARGET_URI_LIST}
};
static GtkTargetList *target_list;
static GdkAtom atom_text_uri_list;

static GtkWidget *window=NULL;      /* Main window */

static GList *selection_list=NULL;      /* Contains currently selected items */
static GList *cp_mv_file_list=NULL;    /* List of files to copy or move */
static GList *action_file_list=NULL;   /* List of files to pass to an action in config.h/run_actions[] */
static GList *thumb_queue=NULL;         /* Thumbnail queue */

static const gchar* host_name;
static gchar *home_path;   /* Users home dir */
static gchar *thumb_dir;   /* Users thumbnail directory */

static GIOChannel *inotify_channel;
static GIOChannel *mounts_channel;

static int mount_fd;
static int inotify_fd;
static int parent_wd;      /* Working directory (parent) watch */
static int thumbnail_wd;   /* Thumbnail watch */

static gchar *parent=NULL;   /* The current directory */
static long int n_parent_items=0;

static GtkToolItem *up_button;
static GtkToolItem *home_button;
static GtkToolItem *stop_button;
static GtkToolItem *info_button;

static GtkIconTheme *icon_theme;

static GHashTable *thumb_hash=NULL;   /* Thumbnails in the current view */
static GHashTable *mount_hash=NULL;
static GHashTable *symlink_hash=NULL;
static GHashTable *child_hash=NULL;

static GtkListStore *store=NULL;

static gboolean inotify_handler(GIOChannel *source, GIOCondition condition, gpointer user_data);
static gboolean gen_thumbnails(RFM_flags *rfmFlags);
static void show_all_thumbnails(void);
static void refresh_mount_points(void);
static GtkWidget *show_text(GtkTextBuffer* buffer, gchar *title, gint type);
static void show_msgbox(gchar *msg, gchar *title, gint type);
static int read_char_pipe(gint fd, ssize_t block_size, char **buffer);
static void die(const char *errstr, ...);
static void cleanup(GtkWidget *window , GtkWidget *icon_view);
static void show_child_output(RFM_ChildAttribs *child_attribs);
static void set_parent_path(gchar* path);

/* TODO: Function definitions */

#include "config.h"

static void free_child_attribs(RFM_ChildAttribs *child_attribs)
{
   close(child_attribs->stdOut_fd);
   close(child_attribs->stdErr_fd);
   g_free(child_attribs->stdOut);
   g_free(child_attribs->stdErr);
   g_free(child_attribs->name);
   g_free(child_attribs);
}

/* Supervise the children to prevent blocked pipes */
static gboolean child_supervisor(gpointer user_data)
{
   GList *child_hash_values=NULL;
   GList *listElement=NULL;
   RFM_ChildAttribs *child_attribs=NULL;

   if (g_hash_table_size(child_hash)==0)
      return FALSE;
   child_hash_values=g_hash_table_get_values(child_hash);
   listElement=g_list_first(child_hash_values);

   while (listElement != NULL) {
      child_attribs=(RFM_ChildAttribs*)child_hash_values->data;
      read_char_pipe(child_attribs->stdOut_fd,PIPE_SZ,&child_attribs->stdOut);
      read_char_pipe(child_attribs->stdErr_fd,PIPE_SZ,&child_attribs->stdErr);
      if (child_attribs->status!=-1) {
         show_child_output(child_attribs);
         break; /* Ignore other children until called again: child_hash in an unknown state */
      }
      listElement=g_list_next(listElement);
   }
   return TRUE;
}

static void exec_child_handler(GPid pid, gint status, RFM_ChildAttribs *child_attribs)
{
   child_attribs->status=status;
}

static void thumb_child_handler(GPid pid, gint status, RFM_flags *rfmFlags)
{
   rfmFlags->rfm_thumbJobs=FALSE;
}

static void show_child_output(RFM_ChildAttribs *child_attribs)
{
   char *msg=NULL;
   int exitCode=-1;

   if (WIFEXITED(child_attribs->status))
      exitCode=WEXITSTATUS(child_attribs->status);

   if (child_attribs->stdOut!=NULL) {
      if (child_attribs->runOpts==RFM_EXEC_TEXT)
         show_msgbox(child_attribs->stdOut, child_attribs->name, -1);
      else if (child_attribs->runOpts==RFM_EXEC_PANGO)
         show_msgbox(child_attribs->stdOut, child_attribs->name, -2);
      else {
         show_msgbox(child_attribs->stdOut, child_attribs->name, GTK_MESSAGE_INFO);
         if (child_attribs->runOpts==RFM_EXEC_MOUNT && exitCode==0)
            set_parent_path(RFM_MOUNT_MEDIA_PATH);
      }
   }
   if (child_attribs->stdErr!=NULL) {
      msg=g_strdup_printf("%s (%i): Finished with exit code %i.\n\n%s",child_attribs->name,child_attribs->pid,exitCode,child_attribs->stdErr);
      show_msgbox(msg, child_attribs->name, GTK_MESSAGE_ERROR);
      g_free(msg);
   }
   g_hash_table_remove(child_hash,&child_attribs->pid); /* free_child_attribs will be called */
   if (g_hash_table_size(child_hash)==0) gtk_widget_set_sensitive(GTK_WIDGET(info_button),FALSE);
}

static int read_char_pipe(gint fd, ssize_t block_size, char **buffer)
{
   char *txt=NULL;
   char *txtNew=NULL;
   ssize_t read_size=-1;

   txt=malloc((block_size+1)*sizeof(char*));
   if (txt!=NULL)
      read_size=read(fd,txt,block_size);
   if (read_size > 0) {
      txt[read_size]='\0';
      if (*buffer==NULL)
         *buffer=txt;
      else {
         txtNew=realloc(*buffer,(strlen(*buffer)+read_size+1)*sizeof(char*));
         if (txtNew!=NULL) {
            *buffer=txtNew;
            strcat(*buffer,txt);
         }
         g_free(txt);
      }
   }
   else
      g_free(txt);
   return read_size; /* -1 on error */
}

static RFM_defaultPixbufs *load_default_pixbufs(void)
{
   GdkPixbuf *umount_pixbuf;
   GdkPixbuf *mount_pixbuf;
   RFM_defaultPixbufs *defaultPixbufs=NULL;
   
   if(!(defaultPixbufs = calloc(1, sizeof(RFM_defaultPixbufs))))
      return NULL;

   defaultPixbufs->file=gtk_icon_theme_load_icon(icon_theme, "application-octet-stream", RFM_ICON_SIZE, 0, NULL);
   defaultPixbufs->dir=gtk_icon_theme_load_icon(icon_theme, "folder", RFM_ICON_SIZE, 0, NULL);
   defaultPixbufs->symlink=gtk_icon_theme_load_icon(icon_theme, "emblem-symbolic-link", RFM_ICON_SIZE/2, 0, NULL);
   
   umount_pixbuf=gdk_pixbuf_new_from_xpm_data(RFM_icon_unmounted);
   mount_pixbuf=gdk_pixbuf_new_from_xpm_data(RFM_icon_mounted);

   if (defaultPixbufs->file==NULL) defaultPixbufs->file=gdk_pixbuf_new_from_xpm_data(RFM_icon_file);
   if (defaultPixbufs->dir==NULL) defaultPixbufs->dir=gdk_pixbuf_new_from_xpm_data(RFM_icon_folder);
   if (defaultPixbufs->symlink==NULL) defaultPixbufs->symlink=gdk_pixbuf_new_from_xpm_data(RFM_icon_symlink);

   /* Composite images */
   defaultPixbufs->symlinkDir=gdk_pixbuf_copy(defaultPixbufs->dir);
   gdk_pixbuf_composite(defaultPixbufs->symlink,defaultPixbufs->symlinkDir,0,0,gdk_pixbuf_get_width(defaultPixbufs->symlink),gdk_pixbuf_get_height(defaultPixbufs->symlink),0,0,1,1,GDK_INTERP_NEAREST,200);
   defaultPixbufs->unmounted=gdk_pixbuf_copy(defaultPixbufs->dir);
   gdk_pixbuf_composite(umount_pixbuf,defaultPixbufs->unmounted,1,1,gdk_pixbuf_get_width(umount_pixbuf),gdk_pixbuf_get_height(umount_pixbuf),0,0,1,1,GDK_INTERP_NEAREST,100);
   defaultPixbufs->mounted=gdk_pixbuf_copy(defaultPixbufs->dir);
   gdk_pixbuf_composite(mount_pixbuf,defaultPixbufs->mounted,1,1,gdk_pixbuf_get_width(mount_pixbuf),gdk_pixbuf_get_height(mount_pixbuf),0,0,1,1,GDK_INTERP_NEAREST,100);

   g_object_unref(umount_pixbuf);
   g_object_unref(mount_pixbuf);
   
   /* Tool bar icons */
   defaultPixbufs->up=gtk_icon_theme_load_icon(icon_theme, "go-up", RFM_TOOL_SIZE, 0, NULL);
   defaultPixbufs->home=gtk_icon_theme_load_icon(icon_theme, "go-home", RFM_TOOL_SIZE, 0, NULL);
   defaultPixbufs->stop=gtk_icon_theme_load_icon(icon_theme, "process-stop", RFM_TOOL_SIZE, 0, NULL);
   defaultPixbufs->refresh=gtk_icon_theme_load_icon(icon_theme, "view-refresh", RFM_TOOL_SIZE, 0, NULL);
   defaultPixbufs->info=gtk_icon_theme_load_icon(icon_theme, "dialog-information", RFM_TOOL_SIZE, 0, NULL);

   if (defaultPixbufs->up==NULL) defaultPixbufs->up=gdk_pixbuf_new_from_xpm_data(RFM_icon_up);
   if (defaultPixbufs->home==NULL) defaultPixbufs->home=gdk_pixbuf_new_from_xpm_data(RFM_icon_home);
   if (defaultPixbufs->stop==NULL) defaultPixbufs->stop=gdk_pixbuf_new_from_xpm_data(RFM_icon_stop);
   if (defaultPixbufs->refresh==NULL) defaultPixbufs->refresh=gdk_pixbuf_new_from_xpm_data(RFM_icon_refresh);
   if (defaultPixbufs->info==NULL) defaultPixbufs->info=gdk_pixbuf_new_from_xpm_data(RFM_icon_information);
   
   return defaultPixbufs;
}

static void show_msgbox(gchar *msg, gchar *title, gint type)
{
   GtkWidget *dialog;
   gchar *utf8_string=g_locale_to_utf8(msg, -1, NULL, NULL, NULL);

   if (utf8_string==NULL)
      dialog=gtk_message_dialog_new(GTK_WINDOW(window),GTK_DIALOG_DESTROY_WITH_PARENT,GTK_MESSAGE_WARNING,GTK_BUTTONS_OK,"%s","Can't convert message text to UTF8!");
   else {
      if (strlen(msg)>RFM_MX_MSGBOX_CHARS || type==-1) {
         GtkTextBuffer *buffer=gtk_text_buffer_new(NULL);
         gtk_text_buffer_set_text(buffer, utf8_string, -1);
         dialog=show_text(buffer, title, type);
         g_object_unref(buffer);
      }
      else if (type==-2) {
         dialog=gtk_message_dialog_new(GTK_WINDOW(window),GTK_DIALOG_DESTROY_WITH_PARENT,GTK_MESSAGE_INFO,GTK_BUTTONS_OK,NULL);
         gtk_message_dialog_set_markup(GTK_MESSAGE_DIALOG(dialog),utf8_string);
      }
      else
         dialog=gtk_message_dialog_new(GTK_WINDOW(window),GTK_DIALOG_DESTROY_WITH_PARENT,type,GTK_BUTTONS_OK,"%s",utf8_string);
   }
   gtk_window_set_title(GTK_WINDOW(dialog), title);
   g_signal_connect_swapped (dialog, "response",G_CALLBACK(gtk_widget_destroy),dialog);
   gtk_widget_show_all(dialog);
   if (type==GTK_MESSAGE_ERROR)
      gtk_dialog_run(GTK_DIALOG(dialog)); /* Show a modal dialog for errors/warnings */
   g_free(utf8_string);
}

static int show_actionbox(gchar *msg, gchar *title)
{
   GtkWidget *dialog;
   GtkWidget *content_area;
   GtkWidget *title_label;
   GtkWidget *dialog_label;
   gint response_id=GTK_RESPONSE_CANCEL;
   gchar *title_markup=NULL;

   dialog=gtk_dialog_new_with_buttons(title, GTK_WINDOW(window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                       "_Cancel", GTK_RESPONSE_CANCEL,
                                       "_No", GTK_RESPONSE_NO,
                                       "_Yes", GTK_RESPONSE_YES,
                                       "_All", GTK_RESPONSE_ACCEPT,
                                       NULL);
   gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
   dialog_label=gtk_label_new(NULL);
   title_label=gtk_label_new(NULL);
   title_markup=g_strdup_printf("%s%s%s","\n<b><span size=\"large\">",title,":</span></b>\n");
   gtk_label_set_markup(GTK_LABEL(title_label),title_markup);
   g_free(title_markup);
   gtk_label_set_markup(GTK_LABEL(dialog_label),msg);
   content_area=gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   gtk_container_add(GTK_CONTAINER(content_area), title_label);
   gtk_container_add(GTK_CONTAINER(content_area), dialog_label);
   gtk_widget_show_all(dialog);
   response_id=gtk_dialog_run(GTK_DIALOG(dialog));
   gtk_widget_destroy(dialog);
   return response_id;
}

static GtkWidget *show_text(GtkTextBuffer* buffer, gchar *title, gint type)
{
   GtkWidget *text_view;
   GtkWidget *dialog;
   GtkWidget *content_area;
   GtkWidget *sw;

   sw = gtk_scrolled_window_new(NULL, NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);

   dialog=gtk_dialog_new_with_buttons(title,GTK_WINDOW(window),GTK_DIALOG_DESTROY_WITH_PARENT,"_Ok", GTK_RESPONSE_OK, NULL);
   text_view=gtk_text_view_new_with_buffer(buffer);
   gtk_container_add(GTK_CONTAINER(sw), text_view);
   gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view), FALSE);
   gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_NONE);
   gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(text_view), FALSE);

   #ifndef RFM_USE_GTK2
   gtk_text_view_set_monospace(GTK_TEXT_VIEW(text_view), TRUE);
   gtk_widget_set_hexpand(sw, TRUE);
   gtk_widget_set_vexpand(sw, TRUE);
   #endif

   content_area=gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   gtk_container_add(GTK_CONTAINER(content_area), sw);
   gtk_widget_set_size_request(sw, 500, 300);
   
   return dialog;
}

static gint cp_mv_check_path(char *src_path, char *dest_path, gpointer copy)
{
   gchar *src_basename=g_path_get_basename(src_path);
   gchar *dest_basename=g_path_get_basename(dest_path);
   gchar *dest_item=NULL;
   gchar prefixString[32];
   gchar *dialog_label=NULL;
   gchar *dialog_title=NULL;
   gint response_id=GTK_RESPONSE_YES;
   gboolean item_exists=TRUE;
   struct stat src_info;
   struct stat dest_info;
   long long size_diff=0;

   if (stat(dest_path, &dest_info)==0) {
      if (S_ISDIR(dest_info.st_mode)) {
         dest_item=g_strdup_printf("%s/%s",dest_path,src_basename);
         if (stat(dest_item, &dest_info)==0) {
            g_free(dest_basename);
            dest_basename=g_strdup(src_basename);
         }
         else
            item_exists=FALSE;
         g_free(dest_item);
      }
   }
   else
      item_exists=FALSE;

   if (item_exists==TRUE) {
      if (stat(src_path, &src_info)!=0) return GTK_RESPONSE_CANCEL;
      if (dest_info.st_mtime-src_info.st_mtime > 0)
         strcpy(prefixString,"A <b>newer</b>");
      else if (dest_info.st_mtime - src_info.st_mtime < 0)
         strcpy(prefixString,"An <b>older</b>");
      else
         strcpy(prefixString,"The");
      if (S_ISDIR(dest_info.st_mode) && S_ISDIR(src_info.st_mode))
         dialog_label=g_strdup_printf("%s directory %s exists in the destination path.\nMerge contents (existing files may be replaced)?\n",prefixString,dest_basename);
      else {
         size_diff=src_info.st_size-dest_info.st_size;
         dialog_label=g_strdup_printf("%s item <b>%s</b> exists in the destination path.\nOverwrite (net size change = %lli bytes)?\n",prefixString,dest_basename,size_diff);
      }

      if (copy!=NULL)
         dialog_title=g_strdup_printf("Copy %s",src_basename);
      else
         dialog_title=g_strdup_printf("Move %s",src_basename);
      response_id=show_actionbox(dialog_label, dialog_title);
      g_free(dialog_label);
      g_free(dialog_title);
   }
   g_free(src_basename);
   g_free(dest_basename);
   return response_id;
}

static gboolean exec_with_stdOut(gchar **v, gint run_opts)
{
   gboolean rv=FALSE;
   RFM_ChildAttribs *child_attribs=NULL;

   child_attribs=malloc(sizeof(RFM_ChildAttribs));
   if (child_attribs!=NULL) {
      rv=g_spawn_async_with_pipes(parent, v, NULL, G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL,
                                  &child_attribs->pid, NULL, &child_attribs->stdOut_fd,
                                            &child_attribs->stdErr_fd, NULL);
      if (rv==TRUE) {
         fcntl(child_attribs->stdOut_fd, F_SETFL, O_NONBLOCK | fcntl(child_attribs->stdOut_fd, F_GETFL));
         fcntl(child_attribs->stdErr_fd, F_SETFL, O_NONBLOCK | fcntl(child_attribs->stdErr_fd, F_GETFL));
         child_attribs->name=g_strdup(v[0]);
         child_attribs->runOpts=run_opts;
         child_attribs->stdOut=NULL;
         child_attribs->stdErr=NULL;
         child_attribs->status=-1;
         if (g_hash_table_size(child_hash)==0)
            g_timeout_add(100,(GSourceFunc)child_supervisor,NULL);
         g_child_watch_add(child_attribs->pid,(GChildWatchFunc)exec_child_handler,child_attribs);
         g_hash_table_insert(child_hash,&child_attribs->pid,child_attribs);
         gtk_widget_set_sensitive(GTK_WIDGET(info_button),TRUE);
      }
      else
         free(child_attribs);
   }
   return rv;
}

static gchar **build_cmd_vector(const char **cmd, GList *file_list, long n_args, char *dest_path)
{
   long j=0;
   gchar **v=NULL;
   GList *listElement=NULL;

   listElement=g_list_first(file_list);
   if (listElement==NULL) return NULL;

   n_args+=2; /* Account for terminating NULL & possible destination path argument */
   
   if((v=malloc((RFM_MX_ARGS+n_args)*sizeof(gchar*)))==NULL)
      return NULL;

   while (cmd[j]!=NULL && j<RFM_MX_ARGS) {
      v[j]=(gchar*)cmd[j]; /* FIXME: gtk spawn functions require gchar*, but we have const gchar*; should probably g_strdup() and create a free_cmd_vector() function */
      j++;
   }
   if (j==RFM_MX_ARGS) {
      g_warning("build_cmd_vector: %s: RFM_MX_ARGS exceeded. Check config.h!",v[0]);
      free(v);
      return NULL;
   }
   while (listElement!=NULL) {
      v[j]=listElement->data;
      j++;
      listElement=g_list_next(listElement);
   }

   v[j]=dest_path; /* This may be NULL anyway */
   v[++j]=NULL;
   
   return v;
}

static void exec_run_action(const char **action, GList *file_list, long n_args, int run_opts, char *dest_path)
{
   gchar **v=NULL;

   v=build_cmd_vector(action,file_list,n_args,dest_path);
   if (v != NULL) {
      if (run_opts==RFM_EXEC_NONE) {
         if (!g_spawn_async(parent,v,NULL,0,NULL,NULL,NULL,NULL))
            g_warning("exec_run_action: %s failed to execute. Check run_actions[] in config.h!",v[0]);
      }
      else {
         if (!exec_with_stdOut(v, run_opts))
            g_warning("exec_run_action: %s failed to execute. Check run_actions[] in config.h!",v[0]);
      }
      free(v);
   }
   else
      g_warning("exec_run_action: %s failed to execute: build_cmd_vector() returned NULL.",action[0]);
}

static gboolean internal_thumbnailer(gchar *sourcePath, gchar *mime_root, gchar *mime_sub_type, gint64 mtime_file, gchar *thumbPath)
{
   GdkPixbuf *thumb=NULL;
   gchar *mtime_tmp;
   gchar *uri;
   gchar *tmp_thumb_file;

   if (strcmp(mime_root,"image")==0)
      thumb=gdk_pixbuf_new_from_file_at_scale(sourcePath, RFM_THUMBNAIL_SIZE, RFM_THUMBNAIL_SIZE, TRUE, NULL);
   if (thumb!=NULL) {
      mtime_tmp=g_strdup_printf("%lld", mtime_file);
      uri=g_filename_to_uri(sourcePath, NULL, NULL);
      tmp_thumb_file=g_strdup_printf("%s-%s-%ld", thumbPath, PROG_NAME, (long)getpid());

      gdk_pixbuf_save(thumb,tmp_thumb_file, "png", NULL,
            "tEXt::Thumb::MTime", mtime_tmp,
            "tEXt::Thumb::URI", uri,
            "tEXt::Software", PROG_NAME,
            NULL);
      g_object_unref(thumb);
      if (chmod(tmp_thumb_file, S_IRUSR | S_IWUSR)==-1)
         g_warning("internal_thumbnailer: Failed to chmod %s\n", tmp_thumb_file);
      if (rename(tmp_thumb_file,thumbPath)!=0)
         g_warning("internal_thumbnailer: Failed to rename %s\n", tmp_thumb_file);
      g_free(uri);
      g_free(mtime_tmp);
      g_free(tmp_thumb_file);
      return TRUE; /* File was handled */
   }
   return FALSE; /* File not handled */
}

static gboolean external_thumbnailer(gchar *sourcePath, gchar *mime_root, gchar *mime_sub_type, RFM_flags *rfmFlags, gchar *thumbPath)
{
   int t_idx=-1;
   int a_idx=-1;
   int i;
   GPid pid;
   gchar *v[8];

   for(i=0;i<G_N_ELEMENTS(thumbnailers);i++) {   /* Check for user defined thumbnailer */
      if (strcmp(mime_root,thumbnailers[i].thumbRoot)==0 && strcmp(mime_sub_type,thumbnailers[i].thumbSub)==0) {
         t_idx=i;
         break;
      }
      if (strcmp(mime_root,thumbnailers[i].thumbRoot)==0 && strcmp("anything",thumbnailers[i].thumbSub)==0)
         a_idx=i;
   }

   if (t_idx==-1) t_idx=a_idx; /* Maybe we have a generator for all sub types of mimeRoot */

   if (t_idx>-1 && g_file_test(thumbnailers[t_idx].thumbCmdName,G_FILE_TEST_IS_EXECUTABLE)) {
      v[0]=thumbnailers[t_idx].thumbCmdName;
      v[1]=sourcePath;
      v[2]=thumbPath;
      v[3]=g_strdup_printf("%d",RFM_THUMBNAIL_SIZE);
      v[4]=(char*)NULL;
      if (g_spawn_async(parent,v,NULL,G_SPAWN_DO_NOT_REAP_CHILD,NULL,NULL,&pid,NULL)==TRUE)
         g_child_watch_add(pid,(GChildWatchFunc)thumb_child_handler,rfmFlags);

      g_free(v[3]);
      rfmFlags->rfm_thumbJobs=TRUE;
      return TRUE;
   }
   return FALSE;
}

static gboolean gen_thumbnails(RFM_flags *rfmFlags)
{
   gchar *path;
   gchar *mime_root, *mime_sub_type;
   gint64 mtime_file;
   GtkTreeIter iter;
   gchar *c_iter;
   GList *listElement;

   listElement=g_list_first(thumb_queue);
   if (listElement==NULL) {
      gtk_widget_set_sensitive(GTK_WIDGET(stop_button),FALSE);
      return FALSE;   /* End of list */
   }

   c_iter=g_hash_table_lookup(thumb_hash,listElement->data); /* Check that some other program hasn't already generated this one first */
   if (c_iter==NULL) {
      g_free(listElement->data);
      thumb_queue=g_list_delete_link(thumb_queue,listElement);
      return TRUE;
   }
   
   gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(store), &iter, c_iter);
   gtk_tree_model_get (GTK_TREE_MODEL(store), &iter,
                       COL_PATH, &path,
                       COL_MIME_ROOT, &mime_root,
                       COL_MIME_SUB, &mime_sub_type,
                       COL_MTIME, &mtime_file,
                       -1);

   /* Try internal thumbnail generator */
   if (internal_thumbnailer(path, mime_root, mime_sub_type, mtime_file, (gchar*)listElement->data)==TRUE)
      g_free(listElement->data);
   else {
      if (rfmFlags->rfm_thumbJobs==FALSE) { /* Check for running thumbnail processes - only 1 instance at a time */
         external_thumbnailer(path, mime_root, mime_sub_type, rfmFlags, (gchar*)listElement->data);
         g_free(listElement->data);
      }
      else  /* If previous job still running, put this one to end and try later */
         thumb_queue=g_list_append(thumb_queue,listElement->data);
   }
   g_free(path);
   g_free(mime_root);
   g_free(mime_sub_type);
   thumb_queue=g_list_delete_link(thumb_queue,listElement);

   return TRUE;
}

static void load_thumbnail(GtkTreeIter *iter)
{
   gchar *path;
   gchar *mime_sub_type;
   gchar *thumb_path, *md5, *uri;
   GdkPixbuf *pixbuf=NULL;
   gboolean is_dir;
   gboolean is_symlink;
   int not_bin_exec;
   gint64 mtime_file=0;
   gint64 mtime_thumb=1;
   const gchar *tmp=NULL;

   gtk_tree_model_get(GTK_TREE_MODEL(store), iter,
                       COL_PATH, &path,
                       COL_IS_DIRECTORY, &is_dir,
                       COL_MIME_SUB, &mime_sub_type,
                       COL_IS_SYMLINK, &is_symlink,
                       COL_MTIME, &mtime_file,
                       -1);

   not_bin_exec=strcmp(mime_sub_type,"x-executable");
   if (!is_dir && !is_symlink && not_bin_exec) {
      uri=g_filename_to_uri(path, NULL, NULL);
      md5=g_compute_checksum_for_string (G_CHECKSUM_MD5, uri, -1);
      thumb_path=g_strdup_printf("%s/%s.png",thumb_dir, md5);

      /* Could use gdk_pixbuf_new_from_file_at_scale() here to scale to RFM_ICON_SIZE */
      pixbuf = gdk_pixbuf_new_from_file(thumb_path, NULL);
      if (pixbuf != NULL) {
         tmp=gdk_pixbuf_get_option(pixbuf, "tEXt::Thumb::MTime");
         if (tmp!=NULL) mtime_thumb=atol(tmp);
         if (mtime_file!=mtime_thumb) {
            /* Thumbnail exists but is out of date - regenerate; inotify will update */
            g_object_unref(pixbuf);
            thumb_queue=g_list_append(thumb_queue,g_strdup(thumb_path));
         }
         else { /* Thumbnail is valid - add to store */
            gtk_list_store_set(store,iter,COL_PIXBUF,pixbuf,-1);
            g_object_unref(pixbuf);
         }
      }
      else /* No valid thumbnail - generate thumbnail; inotify will update */
         thumb_queue=g_list_append(thumb_queue,g_strdup(thumb_path));

      /* Map thumb path to iter for inotify watch and thumbnail generator */
      g_hash_table_insert(thumb_hash, g_strdup(thumb_path),
                          gtk_tree_model_get_string_from_iter(GTK_TREE_MODEL(store), iter));

      g_free(thumb_path);
      g_free(md5);
      g_free(uri);
   }
   g_free(path);
   g_free(mime_sub_type);
}

static void reset_thumb_job_queue(void)
{
   g_list_foreach(thumb_queue, (GFunc)g_free, NULL);
   g_list_free(thumb_queue);
   thumb_queue=NULL;
}

static void show_all_thumbnails(void)
{
   GtkTreeIter iter;
   gboolean valid;
   RFM_flags *rfmFlags=g_object_get_data(G_OBJECT(window),"rfm_flags");

   g_hash_table_remove_all(thumb_hash);
   reset_thumb_job_queue();

   valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);

   while (valid) {
      load_thumbnail(&iter);
      valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);
   }

   if (thumb_queue!=NULL) {
      rfmFlags->rfm_thumbJobs=FALSE;
      gtk_widget_set_sensitive(GTK_WIDGET(stop_button),TRUE);
      g_idle_add((GSourceFunc)gen_thumbnails, rfmFlags);
   }
}

static gchar *get_mime_type(gchar *path)
{
   GFile *file=NULL;
   GFileInfo *info=NULL;
   gchar *mime_type=NULL;
   
   file=g_file_new_for_path(path);
   info=g_file_query_info(file,G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE, 0, NULL, NULL);
   g_object_unref(file); file=NULL;
   if (info != NULL ) {
      mime_type=g_strdup(g_file_info_get_content_type(info));
      g_object_unref(info); info=NULL;
   }
   else
      mime_type=g_strdup("text/x-generic");
   
   return mime_type;
}

static void add_item(const gchar *name, time_t parent_mtime)
{
   gchar *mime_type=NULL;
   GtkTreeIter iter;
   gchar *icon_name=NULL;
   gchar *mime_root=NULL;
   gchar *mime_sub_type=NULL;
   struct stat file_info;
   struct stat link_info;
   gint64 file_mtime=0;
   gchar *utf8_display_name=NULL;
   GdkPixbuf *pixbuf=NULL;
   gchar *path, *display_name;
   gboolean is_dir=FALSE;
   gboolean is_symlink=FALSE;
   gboolean is_mount_point=FALSE;
   gchar *is_mounted=NULL;
   RFM_defaultPixbufs *defaultPixbufs=g_object_get_data(G_OBJECT(window),"rfm_default_pixbufs");

   if (name[0] == '.') return;

   path=g_build_filename(parent, name, NULL);

   if (stat(path, &file_info)==0) {
      is_dir=(gboolean)S_ISDIR(file_info.st_mode);
      file_mtime=(gint64)file_info.st_mtime;
   }

   if (lstat(path, &link_info)==0)
      is_symlink=(gboolean)S_ISLNK(link_info.st_mode);

   utf8_display_name=g_filename_to_utf8(name, -1, NULL, NULL, NULL);
   if (file_mtime > parent_mtime)
      display_name=g_markup_printf_escaped("<b>%s</b>",utf8_display_name);
   else
      display_name=g_markup_printf_escaped("%s",utf8_display_name);
   g_free(utf8_display_name);

   if (is_dir) {
      is_mount_point=g_hash_table_lookup_extended(mount_hash, path, NULL, (gpointer)&is_mounted);
      mime_root=g_strdup("inode");
      if (is_symlink) {
         mime_sub_type=g_strdup("symlink");
         pixbuf=g_object_ref(defaultPixbufs->symlinkDir);
      }
      else if (is_mount_point) {
         mime_sub_type=g_strdup("mount-point");
         if (is_mounted[0]=='0')
            pixbuf=g_object_ref(defaultPixbufs->unmounted);
         else
            pixbuf=g_object_ref(defaultPixbufs->mounted);
      }
      else {
         mime_sub_type=g_strdup("directory");
         pixbuf=g_object_ref(defaultPixbufs->dir);
      }
   }
   else { /* is_file */
      mime_type=get_mime_type(path);
      mime_root=g_strdup(strtok(mime_type,"/"));
      mime_sub_type=g_strdup(strtok(NULL,"\0"));
      g_free(mime_type);
      icon_name=g_strjoin("-",mime_root,mime_sub_type,NULL);

      if ( !gtk_icon_theme_has_icon(icon_theme, icon_name)) {
         g_free(icon_name);
         #ifdef RFM_ICON_PREFIX
            icon_name=g_strjoin("-",RFM_ICON_PREFIX,mime_root,mime_sub_type,NULL);
            if ( !gtk_icon_theme_has_icon(icon_theme, icon_name)) {
               g_free(icon_name);
         #endif
         icon_name=g_strjoin("-",mime_root,"x-generic",NULL);
         #ifdef RFM_ICON_PREFIX
            }
         #endif
      }

      pixbuf=gtk_icon_theme_load_icon(icon_theme, icon_name, RFM_ICON_SIZE, 0, NULL);
      if (pixbuf==NULL)
         pixbuf=g_object_ref(defaultPixbufs->file);

      if (is_symlink) {
         GdkPixbuf *tmp_pixbuf=NULL;
         tmp_pixbuf=g_hash_table_lookup(symlink_hash,icon_name);
         if (tmp_pixbuf==NULL) {
            tmp_pixbuf=gdk_pixbuf_copy(pixbuf);
            gdk_pixbuf_composite(defaultPixbufs->symlink,tmp_pixbuf,0,0,gdk_pixbuf_get_width(defaultPixbufs->symlink),gdk_pixbuf_get_height(defaultPixbufs->symlink),0,0,1,1,GDK_INTERP_NEAREST,200);
            g_hash_table_insert(symlink_hash, g_strdup(icon_name), g_object_ref(tmp_pixbuf));
            g_object_unref(tmp_pixbuf);
         }
         g_object_unref(pixbuf);
         pixbuf=g_object_ref(tmp_pixbuf);
      }
      g_free(icon_name);
   }
   n_parent_items++;

   gtk_list_store_insert_with_values(store, &iter, n_parent_items,
                       COL_PATH, path,
                       COL_DISPLAY_NAME, display_name,
                       COL_IS_DIRECTORY, is_dir,
                       COL_PIXBUF, pixbuf,
                       COL_MIME_ROOT, mime_root,
                       COL_MIME_SUB, mime_sub_type,
                       COL_IS_SYMLINK, is_symlink,
                       COL_MTIME, file_mtime,
                       -1);

   g_free(path);
   g_free(display_name);
   g_free(mime_root);
   g_free(mime_sub_type);
   g_object_unref(pixbuf);
}

static void fill_store(void)
{
   GDir *dir=NULL;
   const gchar *name=NULL;
   time_t parent_mtime=time(NULL)-parent_mtime_offset;

   //g_warning ("Fill store called!");
   gtk_list_store_clear(store); /* This will g_free and g_object_unref */
   n_parent_items=0;

   dir=g_dir_open(parent, 0, NULL); /* defined in /usr/include/glib-2.0/glib/gdir.h */
   if (!dir) return;

   name=g_dir_read_name(dir);
   while (name!=NULL) {
      add_item(name,parent_mtime);
      name=g_dir_read_name(dir);
   }
   g_dir_close(dir);

   if (do_thumbs==1) show_all_thumbnails();
}

static gint sort_func(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer user_data)
{
   /* Sort directories first */
   gboolean is_dir_a, is_dir_b;
   gchar *name_a, *name_b;
   gchar *clean_a, *clean_b;
   int rv;

   gtk_tree_model_get(model, a,
                      COL_IS_DIRECTORY, &is_dir_a,
                      COL_DISPLAY_NAME, &name_a,
                      -1);

   gtk_tree_model_get(model, b,
                      COL_IS_DIRECTORY, &is_dir_b,
                      COL_DISPLAY_NAME, &name_b,
                      -1);

   if (name_a[0]=='<' && name_a[2]=='>') /* Strip pango markup */
      clean_a=name_a+3;
   else
      clean_a=name_a;

   if (name_b[0]=='<' && name_b[2]=='>')
      clean_b=name_b+3;
   else
      clean_b=name_b;

   if (!is_dir_a && is_dir_b) rv=1;
   else if (is_dir_a && !is_dir_b) rv=-1;
   else rv=g_utf8_collate(clean_a, clean_b);

   g_free(name_a);
   g_free(name_b);

   return rv;
}

static GtkListStore *create_store(void)
{
   GtkListStore *store=gtk_list_store_new(NUM_COLS,
                              G_TYPE_STRING,
                              G_TYPE_STRING,
                              GDK_TYPE_PIXBUF,
                              G_TYPE_BOOLEAN,
                              G_TYPE_STRING,
                              G_TYPE_STRING,
                              G_TYPE_BOOLEAN,
                              G_TYPE_INT64 /* Want time_t really; time_t is 32 bit signed */
                              );

   /* Set sort column and function */
   gtk_tree_sortable_set_default_sort_func(GTK_TREE_SORTABLE(store), sort_func, NULL, NULL);
   gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(store),
                                        GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID,
                                        GTK_SORT_ASCENDING);

   return store;
}

static void set_parent_path(gchar* path)
{
   /* NOTE: Resetting inotify will trigger fill_store()
    * no need to call fill_store here.
    */
   GDir *dir=NULL;
   char *msg=NULL;
   dir = g_dir_open(path, 0, NULL);
   if (dir!=NULL) {
      g_dir_close(dir);
      
      g_free(parent);
      parent=g_strdup(path);

      if (parent_wd >= 0) inotify_rm_watch(inotify_fd, parent_wd);
      parent_wd=inotify_add_watch(inotify_fd, parent, INOTIFY_MASK);
      if (parent_wd < 0) g_warning("set_parent_path: inotify failed\n");
      gtk_window_set_title (GTK_WINDOW (window), parent);
   }
   else {
      msg=g_strdup_printf("%s:\n\nCan't enter directory!",path);
      show_msgbox(msg, "Warning", GTK_MESSAGE_WARNING);
      g_free(msg);
   }
   /* Check status of the up button */
   gtk_widget_set_sensitive(GTK_WIDGET(up_button),strcmp (parent, "/") != 0);
   gtk_widget_set_sensitive(GTK_WIDGET(home_button),strcmp (parent, home_path) != 0);
}

/* Called on a double click from on_button_press() */
static void item_activated(GtkIconView *icon_view, GtkTreePath *tree_path, gpointer user_data)
{
   gchar *path;
   GtkTreeIter iter;
   gboolean is_dir;
   long int i;
   char *mime_root=NULL;
   char *mime_sub_type=NULL;
   long int r_idx=-1;
   gchar *msg;
   GList *file_list=NULL;

   gtk_tree_model_get_iter(GTK_TREE_MODEL(store), &iter, tree_path);

   gtk_tree_model_get(GTK_TREE_MODEL(store), &iter,
                  COL_PATH, &path,
                  COL_IS_DIRECTORY, &is_dir,
                COL_MIME_ROOT, &mime_root,
                COL_MIME_SUB, &mime_sub_type,
                  -1);

   if (showMimeType==1)
      g_print ("rfm: Mime type for %s is %s/%s\n",path,mime_root,mime_sub_type);

   if (!is_dir) {
      file_list=g_list_append(file_list, g_strdup(path));
      for(i=0;i<G_N_ELEMENTS(run_actions);i++) {
         if (strcmp(mime_root, run_actions[i].runRoot)==0) {
            if (strcmp(mime_sub_type, run_actions[i].runSub)==0 || strcmp("anything", run_actions[i].runSub)==0) { /* Exact match */
               r_idx=i;
               break;
            }
            else if (run_actions[i].runSub[0]=='/' && strstr(mime_sub_type, run_actions[i].runSub+1) != NULL) { /* Match characters */
               r_idx=i;
               break;
            }
         }
      }

      if (r_idx != -1)
         exec_run_action(run_actions[r_idx].runCmdName, file_list, 2, run_actions[r_idx].runOpts, NULL);
      else {
         msg=g_strdup_printf("No run action defined for mime type:\n %s/%s\n", mime_root, mime_sub_type);
         show_msgbox(msg, "Run Action", GTK_MESSAGE_INFO);
         g_free(msg);
      }
      g_list_foreach(file_list, (GFunc)g_free, NULL);
      g_list_free(file_list);
   }
   else /* If dir, reset parent path and fill the model */
      set_parent_path(path);

   g_free(path);
   g_free(mime_root);
   g_free(mime_sub_type);
}

static GString* selection_list_to_uri()
{
   gchar *path;
   GtkTreeIter iter;
   gboolean is_dir;
   GList *listElement;
   GString   *string;
   char* uri=NULL;

   string=g_string_new(NULL);
   listElement=g_list_first(selection_list);

   while (listElement != NULL) {
      gtk_tree_model_get_iter(GTK_TREE_MODEL(store), &iter, listElement->data);
      gtk_tree_model_get(GTK_TREE_MODEL(store), &iter,
                         COL_PATH, &path,
                         COL_IS_DIRECTORY, &is_dir,
                         -1);
      /*uri=encode_path_as_uri(path);*/
      #ifdef RFM_SEND_DND_HOST_NAMES
         uri=g_filename_to_uri(path, host_name, NULL);
      #else
         uri=g_filename_to_uri(path, NULL, NULL);
      #endif
      g_string_append(string, uri);
      g_string_append(string, "\r\n");
      g_free (path);
      g_free(uri);
      listElement=g_list_next(listElement);
   }
   return string;
}

static void selection_changed(GtkIconView *icon_view, gpointer user_data)
{
   /* Erase the previous list */
   g_list_foreach(selection_list, (GFunc)gtk_tree_path_free, NULL);
   g_list_free(selection_list);

   /* obtain selected items as a GList: returns NULL if no items selected */
   selection_list=gtk_icon_view_get_selected_items(icon_view);
}

static void up_clicked(GtkToolItem *item, gpointer user_data)
{
   gchar *dir_name;

   dir_name=g_path_get_dirname(parent);
   set_parent_path(dir_name);
   g_free(dir_name);
}

static void home_clicked(GtkToolItem *item, gpointer user_data)
{
   gchar *dir_name;

   dir_name=g_strdup(home_path);
   set_parent_path(dir_name);
   g_free(dir_name);
}

static void stop_clicked(GtkToolItem *item, gpointer user_data)
{
   reset_thumb_job_queue();
}

static void refresh_clicked(GtkToolItem *item, GdkEventButton *event, gpointer user_data)
{
   if (event->button==3) {
      if (gtk_tree_sortable_get_sort_column_id(GTK_TREE_SORTABLE(store),NULL,NULL)==FALSE)
         gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(store), COL_MTIME, GTK_SORT_DESCENDING);   /* Sort in mtime order */
      else
         gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(store),
                                              GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID,
                                              GTK_SORT_ASCENDING);
   }
   refresh_mount_points();
   fill_store();
}

static void info_clicked(GtkToolItem *item, gpointer user_data)
{
   GList *child_hash_values=NULL;;
   GList *listElement=NULL;
   GString   *string=NULL;
   RFM_ChildAttribs *child_attribs=NULL;
   gchar *msg=NULL;

   string=g_string_new("Running processes:\npid: name\n");
   
   child_hash_values=g_hash_table_get_values(child_hash);
   listElement=g_list_first(child_hash_values);

   while (listElement != NULL) {
      child_attribs=(RFM_ChildAttribs*)listElement->data;
      g_string_append_printf(string,"%i: ",child_attribs->pid);
      g_string_append(string, child_attribs->name);
      g_string_append(string, "\n");
      listElement=g_list_next(listElement);
   }

   msg=g_string_free(string,FALSE);
   show_msgbox(msg, "Running Processes", GTK_MESSAGE_INFO);
   g_free(msg);
   g_list_free(child_hash_values);
}

static void exec_user_tool(GtkToolItem *item, RFM_ToolButtons *tool)
{
   if (tool->func==NULL) /* Assume argument is a shell exec */
      g_spawn_async(parent,(gchar**)tool->args,NULL,0,NULL,NULL,NULL,NULL);
   else /* Argument is an internal function */
      tool->func(tool->args);
}

static void free_cp_mv_file_list()
{
   g_list_foreach(cp_mv_file_list,(GFunc)g_free,NULL);
   g_list_free(cp_mv_file_list);
   cp_mv_file_list=NULL;
}

static void cp_mv_file(gpointer doCopy)
{
   GList *listElement=NULL;
   gint response_id=GTK_RESPONSE_CANCEL;
   gint i;
   GList *file_list=NULL;
   gchar *dest_path=NULL;

   /* Destination path is stored in the first element */
   listElement=g_list_first(cp_mv_file_list);
   dest_path=g_strdup(listElement->data);
   listElement=g_list_next(listElement);
   
   i=0;
   while (listElement!=NULL) {
      if (response_id!=GTK_RESPONSE_ACCEPT)
         response_id=cp_mv_check_path(listElement->data, dest_path, doCopy);
      if (response_id==GTK_RESPONSE_YES || response_id==GTK_RESPONSE_ACCEPT) {
         file_list=g_list_append(file_list, g_strdup(listElement->data));
         i++;
      }
       else if (response_id==GTK_RESPONSE_CANCEL)
          break;
      listElement=g_list_next(listElement);
   }

   if (file_list!=NULL) {
      if (response_id!=GTK_RESPONSE_CANCEL) {
         if (doCopy!=NULL)
            exec_run_action(run_actions[0].runCmdName, file_list, i, run_actions[0].runOpts, dest_path);
         else
            exec_run_action(run_actions[1].runCmdName, file_list, i, run_actions[1].runOpts, dest_path);
      }
      g_list_foreach(file_list, (GFunc)g_free, NULL);
      g_list_free(file_list);
   }
   g_free(dest_path);
}

static void dnd_menu_cp_mv(GtkWidget *menuitem, gpointer doCopy)
{
   cp_mv_file(doCopy);
}

/* Receive data from drag initiator after requesting send from drag_drop_handl */
static void drag_data_received_handl(GtkWidget *widget, GdkDragContext *context, gint x, gint y, GtkSelectionData *selection_data, guint target_type, guint time, RFM_dndMenu *dndMenu)
{
   char *uri_list=NULL;
   gint selection_length;
   gboolean data_ok=TRUE;
   char *uri=NULL;
   gchar *src_path=NULL;
   gchar *drop_host=NULL;
   GtkTreePath *tree_path;
   GtkTreeIter iter;
   gchar *path;
   gboolean is_dir;
   GdkDragAction src_actions;
   GdkDragAction selected_action;
   gint bx,by;
   gchar *dest_path=NULL;

   free_cp_mv_file_list();
   /* src_actions holds the bit mask of possible actions if
    * suggested_action is GDK_ACTION_ASK. We ignore suggested
    * action and always ask - see drag_motion_handl() below.
    */
   src_actions=gdk_drag_context_get_actions(context);
   selected_action=gdk_drag_context_get_selected_action(context);
   
   if (selected_action!=GDK_ACTION_ASK)
      g_warning ("drag_data_received_handl(): selected action is not ask!\n");

   selection_length=gtk_selection_data_get_length(selection_data);
   if (target_type==TARGET_URI_LIST && selection_length>0) {
      /* Setup destination */
      gtk_icon_view_convert_widget_to_bin_window_coords(GTK_ICON_VIEW(widget),x,y,&bx,&by);
      tree_path=gtk_icon_view_get_path_at_pos(GTK_ICON_VIEW(widget),bx,by);
      if (tree_path!=NULL) {
         gtk_tree_model_get_iter(GTK_TREE_MODEL(store), &iter, tree_path);
         gtk_tree_model_get(GTK_TREE_MODEL(store), &iter,
                            COL_PATH, &path,
                            COL_IS_DIRECTORY, &is_dir,
                            -1);
         gtk_tree_path_free(tree_path);
         if (is_dir)
            dest_path=g_strdup(path);
         g_free(path);
      }
      
      if (dest_path==NULL)
         dest_path=g_strdup(parent);
      
      /* Store the destination path in the first element */
      cp_mv_file_list=g_list_append(cp_mv_file_list, dest_path);
      dest_path=NULL; /* reference passed to GList - don't free */

      /* Set up source and do requested action */
      uri_list=(char*)gtk_selection_data_get_data(selection_data);
      uri=strtok(uri_list,"\r\n");
      while (uri!=NULL) {
         if (strncmp(uri,"file://",7)!=0) {
            show_msgbox("Wrong scheme: uri list of type file:// expected.\n", "Error", GTK_MESSAGE_ERROR);
            data_ok=FALSE;
            break;
         }

         src_path=g_filename_from_uri(uri, &drop_host, NULL);
         
         if (src_path==NULL) {
            data_ok=FALSE;
            break;
         }
         if (drop_host!=NULL)
            if (strncmp(drop_host,"localhost",9)!=0 && strcmp(drop_host,host_name)!=0) {
               show_msgbox("The file is not on the local host.\n Remote files are not handled.\n", "Error", GTK_MESSAGE_ERROR);
               g_free(drop_host);
               data_ok=FALSE;
               break;
            }

         cp_mv_file_list=g_list_append(cp_mv_file_list,src_path);
         src_path=NULL; /* reference passed to GList - don't free */
         uri=strtok(NULL,"\r\n");
      }
   }
   else
      data_ok=FALSE;
   
   gtk_drag_finish(context,data_ok,FALSE,time);

   if (data_ok==TRUE) {
      gtk_widget_set_sensitive(GTK_WIDGET(dndMenu->copy),FALSE);
      gtk_widget_set_sensitive(GTK_WIDGET(dndMenu->move),FALSE);

      if (src_actions&GDK_ACTION_COPY)
         gtk_widget_set_sensitive(GTK_WIDGET(dndMenu->copy),TRUE);
      if (src_actions&GDK_ACTION_MOVE)
         gtk_widget_set_sensitive(GTK_WIDGET(dndMenu->move),TRUE);
      gtk_menu_popup(GTK_MENU(dndMenu->menu), NULL, NULL, NULL, NULL, 0, time);
   }
}

/* Called when item is dragged over iconview */
static gboolean drag_motion_handl(GtkWidget *widget, GdkDragContext *context, gint x, gint y, guint time, RFM_flags *rfmFlags)
{
   GtkTreePath *tree_path;
   GtkTreeIter iter;
   gboolean is_dir=FALSE;
   gint bx,by;

   gtk_icon_view_convert_widget_to_bin_window_coords(GTK_ICON_VIEW(widget), x, y, &bx, &by);
   tree_path = gtk_icon_view_get_path_at_pos(GTK_ICON_VIEW(widget), bx, by);
   if (tree_path!=NULL) {
      /* Don't drop on self */
      if (! gtk_icon_view_path_is_selected(GTK_ICON_VIEW(widget), tree_path)) {
         gtk_tree_model_get_iter(GTK_TREE_MODEL(store), &iter, tree_path);
         gtk_tree_model_get(GTK_TREE_MODEL (store), &iter,
                     COL_IS_DIRECTORY, &is_dir,
                     -1);
         if (is_dir)
            gtk_icon_view_set_drag_dest_item (GTK_ICON_VIEW(widget), tree_path, GTK_ICON_VIEW_DROP_INTO);
         else
            gtk_icon_view_set_drag_dest_item (GTK_ICON_VIEW(widget), NULL, GTK_ICON_VIEW_DROP_INTO);
      }
      gtk_tree_path_free(tree_path);
   }

   if (!is_dir && rfmFlags->rfm_localDrag)
      gdk_drag_status(context, 0, time); /* Don't drop on source window */
   else
      gdk_drag_status(context, GDK_ACTION_ASK, time); /* Always ask */
   return  TRUE;
}

/* Called when a drop occurs on iconview: then request data from drag initiator */
static gboolean drag_drop_handl(GtkWidget *widget, GdkDragContext *context, gint x, gint y, guint time, gpointer user_data)
{
   GdkAtom target_type;

   target_type = gtk_drag_dest_find_target(widget, context,NULL);
   if (target_type == GDK_NONE) {
      show_msgbox("Only text/uri-lists accepted!\n", "Error", GTK_MESSAGE_ERROR);
      gtk_drag_finish(context,FALSE,FALSE,time);
      return FALSE; /* Can't handle incoming data */
   }
   gtk_drag_get_data(widget,context,target_type,time);
   return TRUE;
}

static void drag_local_handl(GtkWidget *widget, GdkDragContext *context, RFM_flags *rfmFlags)
{
   rfmFlags->rfm_localDrag=!(rfmFlags->rfm_localDrag);
}

/* Send data to drop target after receiving a request for data */
static void drag_data_get_handl(GtkWidget *widget, GdkDragContext *context, GtkSelectionData *selection_data, guint target_type, guint time, gpointer user_data)
{
   GString *text_uri_list;

   text_uri_list=selection_list_to_uri();
   if (target_type==TARGET_URI_LIST) {
      gtk_selection_data_set (selection_data,
                              atom_text_uri_list,
                              8,
                              (guchar*)text_uri_list->str,
                              text_uri_list->len);
   }
   else
      g_warning("drag_data_get_handl: Target type not available\n");
   g_string_free(text_uri_list, TRUE);
}

static void new_item(GtkWidget *menuitem, gpointer newFile)
{
   GtkWidget *content_area;
   GtkWidget *dialog;
   GtkWidget *path_entry;
   GtkWidget *dialog_label;
   gchar *dialog_title=NULL;
   gchar *dialog_label_text=NULL;
   gchar *dest_path=NULL;
   gint response_id;
   FILE *nFile;
   RFM_defaultPixbufs *defaultPixbufs=g_object_get_data(G_OBJECT(window),"rfm_default_pixbufs");

   if (newFile!=NULL) {
      dialog_title=g_strdup_printf("New File");
      dialog_label_text=g_strdup_printf("\n<b><span size=\"large\">Create blank file:</span></b>\n");
   }
   else {
      dialog_title=g_strdup_printf("New Directory");
      dialog_label_text=g_strdup_printf("\n<b><span size=\"large\">Create directory:</span></b>\n");
   }
   dialog=gtk_dialog_new_with_buttons (dialog_title,GTK_WINDOW (window),GTK_DIALOG_MODAL,
                                       "_Cancel", GTK_RESPONSE_CANCEL,
                                       "_Ok", GTK_RESPONSE_OK,
                                       NULL);
   gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
   dialog_label=gtk_label_new (NULL);
   gtk_label_set_markup (GTK_LABEL(dialog_label),dialog_label_text);
   path_entry=gtk_entry_new();
   gtk_entry_set_activates_default(GTK_ENTRY(path_entry), TRUE);
   gtk_entry_set_text(GTK_ENTRY(path_entry),parent);
   if (newFile!=NULL)
      gtk_entry_set_icon_from_pixbuf(GTK_ENTRY(path_entry),GTK_ENTRY_ICON_PRIMARY,defaultPixbufs->file);
   else
      gtk_entry_set_icon_from_pixbuf(GTK_ENTRY(path_entry),GTK_ENTRY_ICON_PRIMARY,defaultPixbufs->dir);
   
   content_area=gtk_dialog_get_content_area(GTK_DIALOG (dialog));
   gtk_container_add (GTK_CONTAINER(content_area), dialog_label);
   gtk_container_add (GTK_CONTAINER(content_area), path_entry);
   gtk_widget_grab_focus(path_entry);
   gtk_widget_show_all(dialog);

   response_id=gtk_dialog_run(GTK_DIALOG(dialog));
   if (response_id==GTK_RESPONSE_OK) {
      dest_path=gtk_editable_get_chars(GTK_EDITABLE (path_entry), 0, -1);
      if (dest_path[0]!='/')
         show_msgbox("Can't create item: full path required\n", "Error", GTK_MESSAGE_ERROR);
      else {
         if (g_file_test(dest_path, G_FILE_TEST_EXISTS))
            show_msgbox("New file/dir aborted: path exists!\n", "Error", GTK_MESSAGE_ERROR);
         else {   
            if (newFile!=NULL) {
                  nFile = fopen (dest_path,"w");
                  if (nFile!=NULL) fclose (nFile);
                  else show_msgbox("Can't create file!\n", "Error", GTK_MESSAGE_ERROR);
            }
            else {
               if (mkdir(dest_path,0777)!=0)
                  show_msgbox("Can't create directory!\n", "Error", GTK_MESSAGE_ERROR);
            }
         }
      }
      g_free(dest_path);
   }
   g_free(dialog_title);
   g_free(dialog_label_text);
   gtk_widget_destroy(dialog);
}

static void copy_parent_to_clipboard(GtkWidget *menuitem, gpointer user_data)
{
   /* Clipboards: GDK_SELECTION_CLIPBOARD, GDK_SELECTION_PRIMARY, GDK_SELECTION_SECONDARY */
   GtkClipboard *clipboard = gtk_clipboard_get(GDK_SELECTION_PRIMARY);
   gtk_clipboard_set_text(clipboard, parent, -1);
}

static void file_menu_cp_mv(GtkWidget *menuitem, gpointer doCopy)
{
   GtkWidget *content_area;
   GtkWidget *dialog;
   GtkWidget *path_entry;
   GtkWidget *dialog_label;
   gchar *src_path=NULL;
   gint response_id;
   GtkTreeIter iter;
   GList *listElement;
   gchar *dialog_title=NULL;
   gchar *dialog_label_text=NULL;
   GdkPixbuf *pixbuf=NULL;

   listElement=g_list_first(selection_list);
   gtk_tree_model_get_iter(GTK_TREE_MODEL (store), &iter, listElement->data);
   gtk_tree_model_get(GTK_TREE_MODEL (store), &iter,
                       COL_PATH, &src_path,
                       COL_PIXBUF, &pixbuf,
                       -1);

   if (doCopy!=NULL) {
      dialog_title=g_strdup_printf("Copy File");
      dialog_label_text=g_strdup_printf("\n<b><span size=\"large\">Copy To:</span></b>\n");
   }
   else {
      dialog_title=g_strdup_printf("Move File");
      dialog_label_text=g_strdup_printf("\n<b><span size=\"large\">Move To:</span></b>\n");
   }

   dialog=gtk_dialog_new_with_buttons(dialog_title,GTK_WINDOW (window),GTK_DIALOG_MODAL,
                                       "_Cancel", GTK_RESPONSE_CANCEL,
                                       "_Ok", GTK_RESPONSE_OK,
                                       NULL);
   gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
   dialog_label=gtk_label_new(NULL);
   gtk_label_set_markup(GTK_LABEL(dialog_label), dialog_label_text);
   
   path_entry=gtk_entry_new();
   gtk_entry_set_activates_default(GTK_ENTRY(path_entry), TRUE);
   gtk_entry_set_text(GTK_ENTRY(path_entry), src_path);
   gtk_entry_set_width_chars(GTK_ENTRY(path_entry), strlen(src_path));
   gtk_entry_set_icon_from_pixbuf(GTK_ENTRY(path_entry),GTK_ENTRY_ICON_PRIMARY,pixbuf);
   
   content_area=gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   gtk_container_add(GTK_CONTAINER(content_area), dialog_label);
   gtk_container_add(GTK_CONTAINER(content_area), path_entry);
   gtk_widget_grab_focus(path_entry);
   gtk_widget_show_all(dialog);

   response_id=gtk_dialog_run(GTK_DIALOG(dialog));
   if (response_id==GTK_RESPONSE_OK) {
      free_cp_mv_file_list();
      cp_mv_file_list=g_list_append(cp_mv_file_list, gtk_editable_get_chars(GTK_EDITABLE(path_entry), 0, -1));
      cp_mv_file_list=g_list_append(cp_mv_file_list, g_strdup(src_path));
      cp_mv_file(doCopy);
   }
   g_free(src_path);
   g_free(dialog_title);
   g_free(dialog_label_text);
   gtk_widget_destroy(dialog);
}

static void file_menu_rm(GtkWidget *menuitem, gpointer user_data)
{
   gchar *path=NULL;
   gchar *display_name=NULL;
   GtkTreeIter iter;
   gboolean is_dir;
   GList *listElement;
   gchar *dialog_label_text=NULL;
   gint response_id=GTK_RESPONSE_CANCEL;
   gboolean delete_to_last=FALSE;
   GList *file_list=NULL;
   gint i=0;

   listElement=g_list_first(selection_list);
   while (listElement!=NULL) {
      gtk_tree_model_get_iter(GTK_TREE_MODEL(store), &iter, listElement->data);
      gtk_tree_model_get(GTK_TREE_MODEL(store), &iter,
                          COL_PATH, &path,
                          COL_DISPLAY_NAME, &display_name,
                          COL_IS_DIRECTORY, &is_dir,
                          -1);
      if (!delete_to_last) {
         if (is_dir)
            dialog_label_text=g_strdup_printf("Delete the directory <b>%s</b>?\n",display_name);
         else
            dialog_label_text=g_strdup_printf("Delete the file <b>%s</b>?\n",display_name);

         response_id=show_actionbox(dialog_label_text,"Delete");
         g_free(dialog_label_text);
      }
      g_free(display_name);
      if (response_id==GTK_RESPONSE_ACCEPT) delete_to_last=TRUE;
      if (response_id==GTK_RESPONSE_YES || response_id==GTK_RESPONSE_ACCEPT) {
         file_list=g_list_append(file_list,g_strdup(path));
         i++;   
      }
      else if (response_id==GTK_RESPONSE_CANCEL)
         break;
      listElement=g_list_next(listElement);
      g_free(path);
   }

   if (file_list!=NULL) {
      if (response_id!=GTK_RESPONSE_CANCEL)
         exec_run_action(run_actions[2].runCmdName, file_list, i, run_actions[2].runOpts, NULL);
      g_list_foreach(file_list, (GFunc)g_free, NULL);
      g_list_free(file_list);
   }
}

static void file_menu_exec(GtkMenuItem *menuitem, RFM_RunActions *selectedAction)
{
   exec_run_action(selectedAction->runCmdName, action_file_list, n_parent_items, selectedAction->runOpts, NULL);
}

RFM_fileMenu *setup_file_menu(void)
{
   gint i;
   RFM_fileMenu *fileMenu=NULL;

   if(!(fileMenu = calloc(1, sizeof(RFM_fileMenu))))
      return NULL;
   if(!(fileMenu->action = calloc(G_N_ELEMENTS(run_actions), sizeof(fileMenu->action)))) {
      free(fileMenu);
      return NULL;
   }
   fileMenu->menu=gtk_menu_new();
   for (i=0;i<2;i++) {
      fileMenu->action[i]=gtk_menu_item_new_with_label(run_actions[i].runName);
      gtk_widget_show(fileMenu->action[i]);
      gtk_menu_shell_append(GTK_MENU_SHELL(fileMenu->menu),fileMenu->action[i]);
   }
   g_signal_connect(fileMenu->action[0],"activate",G_CALLBACK (file_menu_cp_mv),fileMenu->action[0]);   /* Copy item */
   g_signal_connect(fileMenu->action[1],"activate",G_CALLBACK (file_menu_cp_mv),NULL);                  /* Move item */

   fileMenu->separator[0]=gtk_separator_menu_item_new();
   gtk_widget_show(fileMenu->separator[0]);
   gtk_menu_shell_append(GTK_MENU_SHELL(fileMenu->menu),fileMenu->separator[0]);

   fileMenu->action[2]=gtk_menu_item_new_with_label(run_actions[2].runName);
   gtk_widget_show(fileMenu->action[2]);
   gtk_menu_shell_append(GTK_MENU_SHELL(fileMenu->menu),fileMenu->action[2]);
   g_signal_connect(fileMenu->action[2],"activate",G_CALLBACK(file_menu_rm),NULL);

   fileMenu->separator[1]=gtk_separator_menu_item_new();
   gtk_widget_show(fileMenu->separator[1]);
   gtk_menu_shell_append(GTK_MENU_SHELL(fileMenu->menu),fileMenu->separator[1]);

   for(i=3;i<G_N_ELEMENTS(run_actions);i++) {
      fileMenu->action[i]=gtk_menu_item_new_with_label(run_actions[i].runName);
      gtk_widget_show(fileMenu->action[i]);
      gtk_menu_shell_append(GTK_MENU_SHELL(fileMenu->menu),fileMenu->action[i]);
      g_signal_connect(fileMenu->action[i],"activate",G_CALLBACK(file_menu_exec),&run_actions[i]);
   }
   return fileMenu;
}

static void popup_file_menu(GdkEventButton *event, gboolean single_file, RFM_fileMenu *fileMenu)
{
   int showMenuItem[G_N_ELEMENTS(run_actions)]={0};
   gchar *path;
   gchar *mime_root;
   gchar *mime_sub_type;
   GtkTreeIter iter;
   GList *listElement;
   int i;

   /* Clear the action file list */
   g_list_foreach(action_file_list,(GFunc)g_free,NULL);
   g_list_free(action_file_list);
   action_file_list=NULL;

   listElement=g_list_first(selection_list);

   while(listElement!=NULL) {
      gtk_tree_model_get_iter(GTK_TREE_MODEL(store), &iter, listElement->data);
      gtk_tree_model_get (GTK_TREE_MODEL(store), &iter,
                          COL_PATH, &path,
                          COL_MIME_ROOT, &mime_root,
                          COL_MIME_SUB, &mime_sub_type,
                          -1);

      action_file_list=g_list_append(action_file_list, path);
      path=NULL; /* Pointer ref passed to GList - don't free */
      for(i=3;i<G_N_ELEMENTS(run_actions);i++) {
         if (strcmp(mime_root,run_actions[i].runRoot)==0) {
            if (strcmp(mime_sub_type,run_actions[i].runSub)==0) /* Exact match */
               showMenuItem[i]++;
            else if (strcmp("anything",run_actions[i].runSub)==0)
               showMenuItem[i]++;
            else if (run_actions[i].runSub[0]=='/' && strstr(mime_sub_type, run_actions[i].runSub+1) != NULL) /* Match characters */
               showMenuItem[i]++;
         }
         else if (strcmp("anything",run_actions[i].runRoot)==0)
            showMenuItem[i]++;            
      }
      g_free (mime_root);
      g_free (mime_sub_type);
      listElement=g_list_next(listElement);
   }
   if (single_file) {
      gtk_widget_show(fileMenu->action[0]);
      gtk_widget_show(fileMenu->action[1]);
      gtk_widget_show(fileMenu->separator[0]);
   }
   else {
      gtk_widget_hide(fileMenu->action[0]);
      gtk_widget_hide(fileMenu->action[1]);
      gtk_widget_hide(fileMenu->separator[0]);
   }
   for(i=3;i<G_N_ELEMENTS(run_actions);i++) {
      if (showMenuItem[i]>0)
         gtk_widget_show(fileMenu->action[i]);
      else
         gtk_widget_hide(fileMenu->action[i]);
   }

   gtk_menu_popup(GTK_MENU(fileMenu->menu), NULL, NULL, NULL, NULL, event->button, event->time);
}   

RFM_dndMenu *setup_dnd_menu(void)
{
   RFM_dndMenu *dndMenu=NULL;
   if(!(dndMenu = calloc(1, sizeof(RFM_dndMenu))))
      return NULL;

   dndMenu->menu = gtk_menu_new ();

   dndMenu->copy = gtk_menu_item_new_with_label ("Copy");
   gtk_widget_show (dndMenu->copy);
   gtk_menu_shell_append (GTK_MENU_SHELL (dndMenu->menu),dndMenu->copy);
   g_signal_connect (dndMenu->copy, "activate", G_CALLBACK (dnd_menu_cp_mv), dndMenu->copy);

   dndMenu->move = gtk_menu_item_new_with_label ("Move");
   gtk_widget_show (dndMenu->move);
   gtk_menu_shell_append (GTK_MENU_SHELL (dndMenu->menu), dndMenu->move);
   g_signal_connect (dndMenu->move, "activate", G_CALLBACK (dnd_menu_cp_mv), NULL);
   return dndMenu;
}

static GtkWidget *setup_root_menu(void)
{
   GtkWidget *newFile;
   GtkWidget *newDir;
   GtkWidget *copyPath;
   GtkWidget *menu = gtk_menu_new();

   newFile=gtk_menu_item_new_with_label("New File");
   gtk_widget_show(newFile);
   gtk_menu_shell_append(GTK_MENU_SHELL(menu),newFile);
   g_signal_connect(newFile, "activate", G_CALLBACK(new_item), newFile);

   newDir=gtk_menu_item_new_with_label("New Dir");
   gtk_widget_show(newDir);
   gtk_menu_shell_append(GTK_MENU_SHELL(menu),newDir);
   g_signal_connect(newDir, "activate", G_CALLBACK(new_item), NULL);
   
   copyPath=gtk_menu_item_new_with_label("Copy Path");
   gtk_widget_show(copyPath);
   gtk_menu_shell_append(GTK_MENU_SHELL(menu),copyPath);
   g_signal_connect(copyPath, "activate", G_CALLBACK(copy_parent_to_clipboard), NULL);
   
   return menu;
}

static gboolean on_button_press(GtkWidget *widget, GdkEvent *event, GtkWidget *rootMenu)
{
   GtkTreePath *tree_path=NULL;
   GList *first=NULL;
   gboolean ret_val=FALSE;
   RFM_fileMenu *fileMenu=g_object_get_data(G_OBJECT(window),"rfm_file_menu");
   GdkModifierType state;
   GdkEventButton *eb=(GdkEventButton*)event;

   if (gtk_get_current_event_state(&state)) {
      if (state&GDK_CONTROL_MASK || state&GDK_SHIFT_MASK) {
         gtk_icon_view_unset_model_drag_source(GTK_ICON_VIEW(widget));
         return FALSE; /* If CTRL or SHIFT is down, let icon view handle selections */
      }
   }

   tree_path=gtk_icon_view_get_path_at_pos(GTK_ICON_VIEW(widget), eb->x, eb->y);

   /* Handle double clicks here: item_activated signal is not always triggered under certain circumstances
    * NOTE that single press event below is triggered twice on a double click; i.e. this function is called three times!
    */
   if (eb->type==GDK_2BUTTON_PRESS) {
      if (tree_path!=NULL) item_activated(NULL, tree_path, NULL);
      ret_val=TRUE; /* Don't trigger item_activated signal */
   }

   if (eb->type==GDK_BUTTON_PRESS) {
      if (!tree_path) {
         if (eb->button == 3) {
            gtk_icon_view_unselect_all(GTK_ICON_VIEW(widget));
            gtk_menu_popup(GTK_MENU(rootMenu), NULL, NULL, NULL, NULL, eb->button, eb->time);
            ret_val=TRUE;
         }
      }
      else {
         first=g_list_first(selection_list);
         if (eb->button == 1) {
            /* Custom DnD start if multiple items selected */
            if (gtk_icon_view_path_is_selected(GTK_ICON_VIEW(widget),tree_path) && first->next!=NULL) {
               gtk_icon_view_unset_model_drag_source(GTK_ICON_VIEW(widget));
               #ifndef RFM_USE_GTK2
               gtk_drag_begin_with_coordinates(widget,target_list,DND_ACTION_MASK,1,event,eb->x,eb->y);
               #else
               gtk_drag_begin(widget,target_list,DND_ACTION_MASK,1,event);
               #endif
               ret_val=TRUE;
            }
            else
               gtk_icon_view_enable_model_drag_source(GTK_ICON_VIEW(widget),GDK_BUTTON1_MASK,target_entry,N_TARGETS,DND_ACTION_MASK); 
         }
         else if (eb->button == 3) {
            if (gtk_icon_view_path_is_selected(GTK_ICON_VIEW(widget),tree_path) && first->next!=NULL)
               popup_file_menu(eb, FALSE, fileMenu);
            else {
               gtk_icon_view_unselect_all(GTK_ICON_VIEW(widget));
               /* Select the path and trigger selection_changed() to update selection_list */
               gtk_icon_view_select_path(GTK_ICON_VIEW(widget), tree_path);
               popup_file_menu(eb, TRUE, fileMenu);
            }
            ret_val=TRUE;
         }
      }
   }
   gtk_tree_path_free(tree_path);
   return ret_val;
}

static void add_toolbar(GtkWidget *rfm_main_box, RFM_defaultPixbufs *defaultPixbufs)
{
   GtkWidget *tool_bar;
   GtkToolItem *refresh_button;
   GtkToolItem *userButton;
   GtkWidget *buttonImage=NULL; /* Temp store for button image; gtk_tool_button_new() appears to take the reference */
   guint i;

   tool_bar=gtk_toolbar_new();
   gtk_toolbar_set_style(GTK_TOOLBAR(tool_bar), GTK_TOOLBAR_ICONS);
   gtk_box_pack_start(GTK_BOX(rfm_main_box), tool_bar, FALSE, FALSE, 0);

   buttonImage=gtk_image_new_from_pixbuf(defaultPixbufs->up);
   up_button=gtk_tool_button_new(buttonImage,"Up");
   gtk_tool_item_set_is_important(up_button, TRUE);
   gtk_toolbar_insert(GTK_TOOLBAR(tool_bar), up_button, -1);
   g_signal_connect(up_button, "clicked", G_CALLBACK(up_clicked), NULL);

   buttonImage=gtk_image_new_from_pixbuf(defaultPixbufs->home);
   home_button=gtk_tool_button_new(buttonImage,"Home");
   gtk_tool_item_set_is_important(home_button, TRUE);
   gtk_toolbar_insert(GTK_TOOLBAR(tool_bar), home_button, -1);
   g_signal_connect(home_button, "clicked", G_CALLBACK(home_clicked), NULL);

   buttonImage=gtk_image_new_from_pixbuf(defaultPixbufs->stop);
   stop_button=gtk_tool_button_new(buttonImage,"Stop");
   gtk_tool_item_set_is_important(stop_button, TRUE);
   gtk_widget_set_sensitive(GTK_WIDGET(stop_button),FALSE);
   gtk_toolbar_insert(GTK_TOOLBAR(tool_bar), stop_button, -1);
   g_signal_connect(stop_button, "clicked", G_CALLBACK(stop_clicked), NULL);

   buttonImage=gtk_image_new_from_pixbuf(defaultPixbufs->refresh);
   refresh_button=gtk_tool_button_new(buttonImage,"Refresh");
   gtk_toolbar_insert(GTK_TOOLBAR(tool_bar), refresh_button, -1);
   g_signal_connect(refresh_button, "button-press-event", G_CALLBACK(refresh_clicked), NULL);
   
   for(i=0;i<G_N_ELEMENTS(tool_buttons);i++) {

      GdkPixbuf *buttonIcon;
      buttonIcon=gtk_icon_theme_load_icon(icon_theme,tool_buttons[i].buttonIcon , RFM_TOOL_SIZE, 0, NULL);
      buttonImage=gtk_image_new_from_pixbuf(buttonIcon);
      g_object_unref(buttonIcon);

      userButton=gtk_tool_button_new(buttonImage,tool_buttons[i].buttonName);
      gtk_toolbar_insert(GTK_TOOLBAR(tool_bar), userButton, -1);
      g_signal_connect(userButton, "clicked", G_CALLBACK(exec_user_tool), &tool_buttons[i]);
   }
   
   buttonImage=gtk_image_new_from_pixbuf(defaultPixbufs->info);
   info_button=gtk_tool_button_new(buttonImage,"Info");
   gtk_widget_set_sensitive(GTK_WIDGET(info_button),FALSE);
   gtk_toolbar_insert(GTK_TOOLBAR(tool_bar), info_button, -1);
   g_signal_connect(info_button, "clicked", G_CALLBACK(info_clicked), NULL);
}

static GtkWidget *add_iconview(GtkWidget *rfm_main_box, GtkWidget *rootMenu)
{
   GtkWidget *sw;
   GtkWidget *icon_view;
   RFM_dndMenu *dndMenu=g_object_get_data(G_OBJECT(window),"rfm_dnd_menu");
   RFM_flags *rfmFlags=g_object_get_data(G_OBJECT(window),"rfm_flags");

   sw=gtk_scrolled_window_new(NULL, NULL);
   gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw), GTK_SHADOW_ETCHED_IN);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
   gtk_box_pack_start(GTK_BOX (rfm_main_box), sw, TRUE, TRUE, 0);

   icon_view=gtk_icon_view_new_with_model(GTK_TREE_MODEL(store));
   gtk_icon_view_set_selection_mode(GTK_ICON_VIEW(icon_view), GTK_SELECTION_MULTIPLE);
   gtk_icon_view_set_markup_column(GTK_ICON_VIEW (icon_view), COL_DISPLAY_NAME);
   gtk_icon_view_set_pixbuf_column (GTK_ICON_VIEW (icon_view), COL_PIXBUF);
   #ifdef RFM_USE_GTK2
   gtk_icon_view_set_item_width (GTK_ICON_VIEW(icon_view),RFM_THUMBNAIL_SIZE+16);
   #endif
   gtk_drag_dest_set (GTK_WIDGET (icon_view), 0, target_entry, N_TARGETS, DND_ACTION_MASK);

   /* Source DnD signals */
   g_signal_connect (icon_view, "drag-data-get", G_CALLBACK (drag_data_get_handl), NULL);
   g_signal_connect (icon_view, "drag-begin", G_CALLBACK (drag_local_handl), rfmFlags);
   g_signal_connect (icon_view, "drag-end", G_CALLBACK (drag_local_handl), rfmFlags);

   /* Destination DnD signals */
   g_signal_connect (icon_view, "drag-drop", G_CALLBACK (drag_drop_handl), NULL);
   g_signal_connect (icon_view, "drag-data-received", G_CALLBACK (drag_data_received_handl), dndMenu);
   g_signal_connect (icon_view, "drag-motion", G_CALLBACK (drag_motion_handl), rfmFlags);

   g_signal_connect (icon_view, "selection-changed", G_CALLBACK (selection_changed), NULL);
   g_signal_connect (icon_view, "button-press-event", G_CALLBACK(on_button_press), rootMenu);
   
   gtk_container_add (GTK_CONTAINER(sw), icon_view);
   gtk_widget_grab_focus(icon_view);
   
   return icon_view;
}

static gboolean update_thumbnail(gchar *key)
{
   gchar *c_iter=NULL;
   GtkTreeIter iter;
   GdkPixbuf *pixbuf=NULL;

   c_iter=g_hash_table_lookup(thumb_hash,key);
   if (c_iter != NULL) {
      gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(store), &iter, c_iter);
      pixbuf=gdk_pixbuf_new_from_file(key, NULL);
      if (pixbuf!=NULL) {
         gtk_list_store_set(store, &iter, COL_PIXBUF, pixbuf, -1);
         g_object_unref(pixbuf);
      }
   }
   g_free(key); /* Don't free c_iter! */
   return FALSE;
}

static void insert_item(gchar *name, gboolean is_dir)
{
   GdkPixbuf *pixbuf=NULL;
   gchar *mime_root=NULL;
   gchar *mime_sub_type=NULL;
   gboolean is_symlink=FALSE;
   RFM_defaultPixbufs *defaultPixbufs=g_object_get_data(G_OBJECT(window),"rfm_default_pixbufs");
   gchar *utf8_display_name=NULL;
   gchar *display_name;
   GtkTreeIter iter;
   gchar *path=g_build_filename(parent, name, NULL);
   
   if (name[0]=='.') return; /* Don't show hidden files */

   utf8_display_name=g_filename_to_utf8(name, -1, NULL, NULL, NULL);

   if (is_dir) {   /* A new directory was created: add an entry in the store and finish: No need to call fill store */
      mime_root=g_strdup("inode");
      mime_sub_type=g_strdup("directory");
      pixbuf=g_object_ref(defaultPixbufs->dir);
      display_name=g_markup_printf_escaped("<b>%s</b>",utf8_display_name);
   }
   else {   /* A new file was added, but has not completed copying, or is still open: add entry; inotify will call fill_store when complete */
      mime_root=g_strdup("application");
      mime_sub_type=g_strdup("octet-stream");
      pixbuf=g_object_ref(defaultPixbufs->file);
      display_name=g_markup_printf_escaped("<i>%s</i>",utf8_display_name);
   }
   g_free(utf8_display_name);

   n_parent_items++;
   gtk_list_store_insert_with_values(store, &iter, n_parent_items,
                       COL_PATH, path,
                       COL_DISPLAY_NAME, display_name,
                       COL_IS_DIRECTORY, is_dir,
                       COL_PIXBUF, pixbuf,
                       COL_MIME_ROOT, mime_root,
                       COL_MIME_SUB, mime_sub_type,
                       COL_IS_SYMLINK, is_symlink,
                       COL_MTIME, (gint64)time(NULL),
                       -1);
   g_free(path);
   g_free(display_name);
   g_free(mime_root);
   g_free(mime_sub_type);
   g_object_unref(pixbuf);
}

static gboolean inotify_handler(GIOChannel *source, GIOCondition condition, gpointer user_data)
{
   int fd = g_io_channel_unix_get_fd(source);
   char buffer[(sizeof(struct inotify_event)+16)*1024];
   int len=0, i=0;
   gboolean refresh_view=FALSE;
   gchar *key=NULL;

   len=read(fd, buffer, sizeof(buffer));
   if (len<0) {
      g_warning("inotify_handler: inotify read error\n");
      return TRUE;
   }

   while (i<len) {
      struct inotify_event *event=(struct inotify_event *) (buffer+i);

      if (event->len) {
         if (event->wd==thumbnail_wd) {
            /* Update thumbnails in the current view */
            key=g_strdup_printf("%s/%s",thumb_dir,event->name);
            g_idle_add((GSourceFunc)update_thumbnail,key);
         }
         else {
            if (event->mask & IN_CREATE)
               insert_item(event->name, event->mask & IN_ISDIR);
            else
               refresh_view=TRUE; /* Item IN_CLOSE_WRITE, deleted or moved */
         }
      }
      else
         refresh_view=TRUE; /* Watch changed i.e. parent changed */
         
      i+=sizeof(*event)+event->len;

      if (event->mask & IN_DELETE_SELF || event->mask & IN_MOVE_SELF) {
         show_msgbox("Parent directory deleted!", "Error", GTK_MESSAGE_ERROR);
         set_parent_path(home_path);
         refresh_view=TRUE;
      }
   }
   if (refresh_view) fill_store();

   return TRUE;
}

static gboolean init_inotify(void)
{
   inotify_fd = inotify_init();
   if ( inotify_fd < 0 )
      return FALSE;
   else {
      inotify_channel=g_io_channel_unix_new(inotify_fd);
      g_io_add_watch(inotify_channel, G_IO_IN, inotify_handler, NULL);
      g_io_channel_unref(inotify_channel);

      thumbnail_wd=inotify_add_watch(inotify_fd, thumb_dir, IN_MOVE);
      if (thumbnail_wd < 0) g_warning("init_inotify: Failed to add watch on dir %s\n",thumb_dir);
   }
   return TRUE;
}

static gboolean mounts_handler(GIOChannel *source, GIOCondition condition, gpointer user_data)
{
   refresh_mount_points();
   if (!g_file_test(parent,G_FILE_TEST_IS_DIR)) {
      show_msgbox("Filesystem was unmounted!", "Error", GTK_MESSAGE_ERROR);
      set_parent_path(home_path);
   }
   fill_store();
   return TRUE;
}

static void refresh_mount_points(void)
{
   FILE *fstab_fp=NULL;
   FILE *mtab_fp=NULL;
   struct mntent *fstab_entry=NULL;
   struct mntent *mtab_entry=NULL;

   /* NOTE: fstab is not watched for changes; user must use refresh button */
   g_hash_table_remove_all(mount_hash);
   fstab_fp=setmntent("/etc/fstab", "r");
   if (fstab_fp!=NULL) {
      fstab_entry=getmntent(fstab_fp);
      while (fstab_entry!=NULL) {
         g_hash_table_insert(mount_hash, g_strdup(fstab_entry->mnt_dir), g_strdup("0\0"));
         fstab_entry=getmntent(fstab_fp);
      }
      endmntent(fstab_fp);
   }

   mtab_fp=setmntent("/proc/mounts", "r");
   if (mtab_fp!=NULL) {
      mtab_entry=getmntent(mtab_fp);
      while (mtab_entry!=NULL) {
         g_hash_table_insert(mount_hash, g_strdup(mtab_entry->mnt_dir), g_strdup("1\0"));
         mtab_entry=getmntent(mtab_fp);
      }
      endmntent(mtab_fp);
   }
}

static void init_mount_points(void)
{
   mount_fd=open("/proc/mounts", O_RDONLY, 0);
   mounts_channel=g_io_channel_unix_new(mount_fd);
   g_io_add_watch(mounts_channel, G_IO_PRI, mounts_handler, NULL);
   g_io_channel_unref(mounts_channel);
}

static void free_default_pixbufs(RFM_defaultPixbufs *defaultPixbufs)
{
   g_object_unref(defaultPixbufs->file);
   g_object_unref(defaultPixbufs->dir);
   g_object_unref(defaultPixbufs->symlinkDir);
   g_object_unref(defaultPixbufs->unmounted);
   g_object_unref(defaultPixbufs->mounted);
   g_object_unref(defaultPixbufs->symlink);
   g_object_unref(defaultPixbufs->up);
   g_object_unref(defaultPixbufs->home);
   g_object_unref(defaultPixbufs->stop);
   g_object_unref(defaultPixbufs->refresh);
   g_object_unref(defaultPixbufs->info);
   g_free(defaultPixbufs);
}

static int setup(char *initDir)
{
   GtkWidget *rootMenu=NULL;
   GtkWidget *rfm_main_box;
   GtkWidget *icon_view;
   RFM_dndMenu *dndMenu=NULL;
   RFM_fileMenu *fileMenu=NULL;
   RFM_defaultPixbufs *defaultPixbufs=NULL;
   RFM_flags *rfmFlags=NULL;

   rfmFlags=malloc(sizeof(RFM_flags));
   if (rfmFlags==NULL) return 1;
   rfmFlags->rfm_localDrag=FALSE;
   rfmFlags->rfm_thumbJobs=FALSE;

   gtk_init(NULL, NULL);

   window=gtk_window_new(GTK_WINDOW_TOPLEVEL);
   gtk_window_set_default_size(GTK_WINDOW(window), 650, 400);
   gtk_window_set_title(GTK_WINDOW(window),parent);
   #ifndef RFM_SHOW_WINDOW_DECORATIONS
   gtk_window_set_decorated(GTK_WINDOW(window),FALSE);
   #endif
   #ifndef RFM_USE_GTK2
   rfm_main_box=gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
   #else
   rfm_main_box=gtk_vbox_new(FALSE,0);
   #endif
   gtk_container_add(GTK_CONTAINER(window), rfm_main_box);

    host_name=g_get_host_name();
   home_path=g_strdup(g_get_home_dir());
   thumb_dir=g_strdup_printf("%s/.thumbnails/normal",home_path);
   thumb_hash=g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
   mount_hash=g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
   symlink_hash=g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_object_unref);
   child_hash=g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)free_child_attribs);

   if (!g_file_test(thumb_dir,G_FILE_TEST_IS_DIR) && do_thumbs==1) {
      if (g_mkdir_with_parents(thumb_dir,S_IRWXU)!=0) {
         g_warning("Setup: Can't create thumbnail directory.");
         do_thumbs=0;
      }
   }
   
   init_mount_points();   
   init_inotify();

   /* Initialise dnd */
   target_list=gtk_target_list_new(target_entry,N_TARGETS);

   #ifdef RFM_ICON_THEME
      icon_theme=gtk_icon_theme_new();
      gtk_icon_theme_set_custom_theme(icon_theme,RFM_ICON_THEME);
   #else
      icon_theme=gtk_icon_theme_get_default();
   #endif

   atom_text_uri_list=gdk_atom_intern("text/uri-list", FALSE);

   fileMenu=setup_file_menu(); /* TODO: WARNING: This can return NULL! */
   dndMenu=setup_dnd_menu();
   rootMenu=setup_root_menu();
   defaultPixbufs=load_default_pixbufs(); /* TODO: WARNING: This can return NULL! */
   g_object_set_data(G_OBJECT(window),"rfm_file_menu",fileMenu);
   g_object_set_data_full(G_OBJECT(window),"rfm_dnd_menu",dndMenu,(GDestroyNotify)g_free);
   g_object_set_data_full(G_OBJECT(window),"rfm_default_pixbufs",defaultPixbufs,(GDestroyNotify)free_default_pixbufs);
   g_object_set_data_full(G_OBJECT(window),"rfm_flags",rfmFlags,(GDestroyNotify)g_free);
   store=create_store();
   add_toolbar(rfm_main_box,defaultPixbufs);
   icon_view=add_iconview(rfm_main_box,rootMenu);

   g_signal_connect(window,"destroy",G_CALLBACK(cleanup),icon_view);

   if (initDir==NULL)
         set_parent_path(home_path);
   else
      set_parent_path(initDir);

   refresh_mount_points();
   fill_store();
   gtk_widget_show_all(window);
   return 0;
}

static void cleanup(GtkWidget *window , GtkWidget *icon_view)
{
   RFM_fileMenu *fileMenu=g_object_get_data(G_OBJECT(window),"rfm_file_menu");

   gtk_icon_view_unselect_all(GTK_ICON_VIEW(icon_view)); /* selection_list will be freed */
   gtk_target_list_unref(target_list);
   if (child_hash != NULL) {
      if (g_hash_table_size(child_hash)>0)
         g_warning ("Ending program, but background jobs still running!\n");
   }
   gtk_main_quit();

   inotify_rm_watch(inotify_fd, parent_wd);
   inotify_rm_watch(inotify_fd, thumbnail_wd);
   g_io_channel_shutdown(inotify_channel,FALSE,NULL);
   g_io_channel_shutdown(mounts_channel,FALSE,NULL);
   g_io_channel_unref(inotify_channel);
   g_io_channel_unref(mounts_channel);

   g_hash_table_destroy(thumb_hash);
   g_hash_table_destroy(mount_hash);
   g_hash_table_destroy(symlink_hash);
   g_hash_table_destroy(child_hash);

   #ifdef RFM_ICON_THEME
      g_object_unref(icon_theme);
   #endif

   free_cp_mv_file_list();
   
   g_list_foreach(action_file_list, (GFunc)g_free, NULL);
   g_list_free(action_file_list);

   g_free(home_path);
   g_free(thumb_dir);
   g_free(parent);

   g_object_unref(store);
   free(fileMenu->action);
   free(fileMenu);
}

/* From http://dwm.suckless.org/ */
static void die(const char *errstr, ...) {
   va_list ap;

   va_start(ap, errstr);
   vfprintf(stderr, errstr, ap);
   va_end(ap);
   exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
   int i=0;
   char *initDir=NULL;
   struct stat statbuf;
   char cwd[1024]; /* Could use MAX_PATH here from limits.h, but still not guaranteed to be max */

   if (argc >1 && argv[1][0]=='-') {
      switch (argv[1][1]) {
      case 'c':
         if (getcwd(cwd, sizeof(cwd)) != NULL) /* getcwd returns NULL if cwd[] not big enough! */
            initDir=cwd;
         else
            die("ERROR: %s: getcwd() failed.\n",PROG_NAME);
         break;
      case 'd':
         if (argc !=3 || argv[argc-1][0]!='/')
            die("ERROR: %s: A full path is required for the -d option.\n",PROG_NAME);
         i=strlen(argv[2])-1;
         if (i!=0 && argv[2][i]=='/')
            argv[2][i]='\0';
         if (strstr(argv[2],"/.")!=NULL)
            die("ERROR: %s: Hidden files are not supported.\n",PROG_NAME);
         
         if (stat(argv[2],&statbuf)!=0) die("ERROR: %s: Can't stat %s\n",PROG_NAME,argv[2]);
         if (! S_ISDIR(statbuf.st_mode) || access(argv[2], R_OK | X_OK)!=0)
            die("ERROR: %s: Can't enter %s\n",PROG_NAME,argv[2]);
         initDir=argv[2];
         break;
      case 'i':
         if (showMimeType==1) showMimeType=0;
         else showMimeType=1;
         break;
      case 'v':
         die("%s-%s, Copyright (C) 2012-2016 Rodney Padgett, see LICENSE for details\n",PROG_NAME,VERSION);
         break;
      default:
         die("Usage: %s [-c || -d <full path to directory> || -i || -v]\n", PROG_NAME);
      }
   }
   if (setup(initDir)==0)
      gtk_main();
   else
      die("ERROR: %s: setup() failed\n",PROG_NAME);
   return 0;
}
