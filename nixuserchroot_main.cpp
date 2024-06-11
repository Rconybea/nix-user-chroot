/* @file nixuserchroot_main.cpp
 *
 * Modelled after lethalman's version
 *   https://github.com/lethalman/nix-user-chroot.
 *
 * Preferred a c++ version
 */

#define _GNU_SOURCE  // e.g. for ::unshare()

#include <iostream>
#include <string>
//#include <sched.h>
#include <unistd.h>
//#include <stdlib.h>
//#include <sys/wait.h>
//#include <signal.h>
#include <fcntl.h>
//#include <limits.h>
#include <sys/mount.h>
//#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <pwd.h>

template<typename T>
void
error_exit_aux(T && x)
{
    std::cerr << x;
} /*error_exit_aux*/

template <typename T, typename... Tn>
void
error_exit_aux(T && x, Tn&&... rest)
{
  std::cerr << x;
  error_exit_aux (rest...);
} /*error_exit_aux*/

constexpr char const * c_prefix = "error: nixuserchroot: ";

template <typename... Tn>
void
error_exit(Tn&&... args)
{
  if (errno)
    ::error_exit_aux(c_prefix, args..., ": ", ::strerror(errno));
  else
    ::error_exit_aux(c_prefix, args..., ": errno n/a");
  std::cerr << std::endl;

  ::exit(1);
} /*error_exit*/

/* promote char* to std::string,
 * but send nullptr to ""
 */
std::string str(char * x, std::string const & default_str) {
  if (x)
    return x;
  else
    return default_str;
} /*str*/

/* return a new temporary directory,
 * somewhere like
 *   /tmp/nix123456
 */
std::string
establish_unique_dir()
{
  std::string tmpdir = str(::getenv("TMPDIR"), "/tmp/");

  char template_buf[PATH_MAX];
  int err = ::snprintf(template_buf,
               PATH_MAX,
               "%snixXXXXXX",
               tmpdir.c_str());
  if (err < 0) {
    std::cerr << "error: nixuserchroot: TMPDIR too long"
          << " [" << tmpdir << "]"
          << std::endl;
    ::exit (1);
  }

  std::string root_dir = str(::mkdtemp(template_buf), "");
  if (root_dir.empty())
    ::error_exit("mkdtemp(", template_buf, ")");

  return root_dir;
} /*establish_rootdir*/

std::string
verify_nixdir(std::string const & nixdir)
{
  std::string nixdir_canonical = str(::realpath(nixdir.c_str(), nullptr), "");

  if (nixdir_canonical.empty())
    ::error_exit("realpath(", nixdir, ")");

  return nixdir_canonical;
} /*verify_nixdir*/

/* with caveats,  bind the contents of directory (given by src_dir),
 * into directory given by dest_dir.
 *
 * caveats:
 * - skip special entries {src_dir/. src_dir/..}
 * - do not bind src_dir/nix,  if it exists
 *
 * dest_dir should be a new, empty directory
 */
void
bind_dir_contents(std::string const & src_dir,
          std::string const & dest_dir)
{
  DIR * d = ::opendir(src_dir.c_str());
  if (!d) {
    ::error_exit("opendir(", src_dir, ")");
  }

  struct dirent * dir_entry = nullptr;
  while ((dir_entry = ::readdir(d))) {
    char const * subdir = dir_entry->d_name;

    if ((::strcmp(subdir, ".") == 0)
    || (::strcmp(subdir, "..") == 0))
      continue;

    if (::strcmp(subdir, "nix") == 0) {
      std::cerr << "warning: nixuserchroot: ignore existing nix directory [" << src_dir << "nix]" << std::endl;
      continue;
    }

    char path_buf1[PATH_MAX];
    ::snprintf(path_buf1, sizeof(path_buf1),
           "%s%s", src_dir.c_str(), dir_entry->d_name);

    struct stat stat_buf;
    if (::stat(path_buf1, &stat_buf) < 0) {
      /* for example,  control here if src_dir contains a broken symlink */
      std::cerr << c_prefix << "could not stat [" << path_buf1 << "]"
        << ", skip: " << ::strerror(errno)
        << std::endl;
      continue;
    }

    char path_buf2[PATH_MAX];
    ::snprintf(path_buf2, sizeof(path_buf2),
           "%s/%s", dest_dir.c_str(), dir_entry->d_name);

    if (S_ISDIR(stat_buf.st_mode)) {
      ::mkdir(path_buf2, stat_buf.st_mode & ~S_IFMT);

      bool mount_ok = false;
      errno = 0;
#ifdef NOT_USING
      mount_ok = (::mount(path_buf1, path_buf2, "none", MS_BIND | MS_REC, nullptr) == 0);
#endif

      if (!mount_ok) {
    std::cerr << c_prefix << "could not bind mount"
          << " [" << path_buf1 << "] to"
          << " [" << path_buf2 << "]"
          << ", skip: " << ::strerror(errno)
          << std::endl;
      }
    }
  }
} /* bind_dir_contents*/

/* in dest_dir, bind mount name -> parent_dir */
void
bind_dir(std::string const & parent_dir,
     std::string const & name,
     std::string const & dest_dir)
{
  struct stat stat_buf2;
  if (::stat(dest_dir.c_str(), &stat_buf2) < 0) {
    error_exit("stat(", dest_dir, ")");
  }

  char path_buf[PATH_MAX];
  ::snprintf(path_buf, sizeof(path_buf), "%s%s", parent_dir.c_str(), name.c_str());
  ::mkdir(path_buf, stat_buf2.st_mode & ~S_IFMT);

  errno = 0;
  bool mount_ok = false;
#ifdef NOT_USING
  mount_ok = (::mount(dest_dir.c_str(), path_buf, "none", MS_BIND | MS_REC, nullptr) == 0);
#endif

  if (!mount_ok)
    ::error_exit("mount(", dest_dir, ",", path_buf, ")");
} /*bind_dir*/

/* chroot running process to root_dir;
 * preserves original working directory,
 * (provided it's accessible by name from root_dir)
 */
void
chroot_dir(std::string const & root_dir)
{
  char cwd[PATH_MAX];

  if (!::getcwd(cwd, sizeof(cwd)))
    error_exit("getcwd()");

  ::chdir("/");
  if (::chroot(root_dir.c_str()) < 0)
      error_exit("chroot(", root_dir, ")");

  ::chdir(cwd);
} /*chroot_dir*/

int
main(int argc, char * argv[])
{
    std::cerr << "nixuserchroot: starting.." << std::endl;

    uid_t uid = ::getuid();

    struct passwd * pw = ::getpwuid(uid);

    std::string home_dir = str(pw->pw_dir, "");
    if (home_dir.empty())
        ::error_exit("no home directory!");

    /* in user namespace,
     *   /nix will really be the alue of nix_dir_in
     */
    std::string nix_dir_in = str((argc >= 2) ? argv[1] : nullptr,
                                 home_dir + "/.nix");
    std::cerr << "nixuserchroot: nix_dir_in=[" << nix_dir_in << "]" << std::endl;

    std::string nix_dir = ::verify_nixdir(nix_dir_in);
    std::cerr << "nixuserchroot: nix_dir=[" << nix_dir << "]" << std::endl;;

    /* in user namespace,
     *   / will refer to the contents of this (temporary) directory
     */
    std::string root_dir = ::establish_unique_dir ();
    std::cerr << "nixuserchroot: root_dir=[" << root_dir << "]" << std::endl;

    gid_t gid = ::getgid();

    bool unshare_ok = false;

#ifdef NOT_USING
    unshare_ok = (::unshare(CLONE_NEWNS | CLONE_NEWNUSER) < 0);
#endif
    if (!unshare_ok)
        error_exit("unshare(CLONE_NEWNS|CLONE_NEWNUSER)");

    /* bind mount the contents of / (/bin, /etc, /var, ..) into root_dir
     * (excluding /nix,  if present)
     */
    ::bind_dir_contents("/", root_dir);

    ::bind_dir(root_dir, "nix", nix_dir);

    /* 'magic spell': fix an issue where cannot write to /proc/self/gid_map */
    int fd_setgroups = ::open("/proc/self/setgroups", O_WRONLY);
    if (fd_setgroups > 0) {
        ::write(fd_setgroups, "deny", 4);
    }

    /* map original uid/gid in new namespace, to uid=1, gid=1 */

    char map_buf[1024];
    ::snprintf(map_buf, sizeof(map_buf), "%d %d 1", uid, uid);
#ifdef NOT_USING
    update_map(map_buf, "/proc/self/uid_map");
#endif
    std::cerr << "nixuserchroot: mapped uid using [" << map_buf << "]" << std::endl;

    ::snprintf(map_buf, sizeof(map_buf), "%d %d 1", gid, gid);
#ifdef NOT_USING
    update_map(map_buf, "/proc/self/gid_map");
#endif
    std::cerr << "nixuserchroot: mapped gid using [" << map_buf << "]" << std::endl;

    chroot_dir(root_dir);

    /* convenience.. */
    ::setenv("NIX_CONF_DIR", "/nix/etc/nix", 1);

    /* finally,  execute command */
    int cmd_err = 0;
    char * cmd_name = nullptr;

    if (argc < 3) {
        /* default command is "bash" */
        char * cmd_argv[2];

        cmd_name = const_cast<char *>("/bin/bash");
        cmd_argv[0] = cmd_name;
        cmd_argv[1] = nullptr;

        /* default command to "bash" */
        cmd_err = ::execvp(cmd_argv[0], cmd_argv);
    } else {
        cmd_name = argv[2];
        cmd_err = ::execvp(argv[2], argv+2);
    }

    /* control here only if ::execvp() failed */
    ::error_exit("execvp(", cmd_name, ")");
} /*main*/

/* end nixuserchroot_main.cpp */
