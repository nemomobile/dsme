# Normally shutdown and reboot should always go through DSME.
#
# Running reboot/poweroff/etc binaries directly bypasses DSME
# and the shutdown does not work the same way as powering off
# via UI. The differences include for example:
# - Emergency calls/alarms/charger/etc are not considered
# - Applications do not get save-data and pre-shutdown signals
# - Shutdown vibration, led and logo do not get triggered
#
# Use shell functions to allow users to continue using familiar
# commands from interactive shell, but do the shutdown/reboot
# via dsme.
#
# If needed, the real systemd binaries (e.g. reboot) can still
# be invoked by using the full path:
# # /usr/sbin/reboot
#
# Or by ignoring the shell functions via command:
# # command reboot

# Define shell functions for interactive shells only
case "$-" in *i*) ;; *) return ;; esac

# Replace simple poweroff/halt/reboot/shutdown invocations
# with equivalent dsmetool operations

poweroff()
{
  [ "$#" -eq 0 ] && /usr/sbin/dsmetool --shutdown || /usr/sbin/poweroff "$@"
}
halt()
{
  [ "$#" -eq 0 ] && /usr/sbin/dsmetool --shutdown || /usr/sbin/halt "$@"
}
reboot()
{
  [ "$#" -eq 0 ] && /usr/sbin/dsmetool --reboot   || /usr/sbin/reboot "$@"
}
shutdown()
{
  DSME_SHUTDOWN_MODE="--shutdown"
  DSME_SHUTDOWN_TIME=""
  DSME_SHUTDOWN_MESG=""
  DSME_SHUTDOWN_UNKN=""
  for f in "$@"; do
    case "$f" in
      -H|--halt|-P|--poweroff|-h)
        DSME_SHUTDOWN_MODE="--shutdown"
        ;;
      -r|--reboot)
        DSME_SHUTDOWN_MODE="--reboot"
        ;;
      -*)
        DSME_SHUTDOWN_UNKN="y"
        ;;
      *)
        if [ -z "$DSME_SHUTDOWN_TIME" ]; then
          DSME_SHUTDOWN_TIME="$f"
        else
          DSME_SHUTDOWN_MESG="$f"
        fi
        ;;
   esac
  done
  [ "${DSME_SHUTDOWN_TIME:-now}" = "now" ] && \
  [ "$DSME_SHUTDOWN_MESG" = "" ] && \
  [ "$DSME_SHUTDOWN_UNKN" = "" ] && \
  /usr/sbin/dsmetool "$DSME_SHUTDOWN_MODE" || /usr/sbin/shutdown "$@"
  unset DSME_SHUTDOWN_MODE DSME_SHUTDOWN_TIME
  unset DSME_SHUTDOWN_MESG DSME_SHUTDOWN_UNKN
}
