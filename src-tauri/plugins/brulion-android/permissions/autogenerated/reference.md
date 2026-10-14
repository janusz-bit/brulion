## Default Permission

Everything Brulion's frontend needs from the Android plugin: checking and requesting
all-files access (Syncthing folders on shared storage), the notification and exact-alarm
permissions, replacing the set of native reminders, reading the notification tap that
opened the app and listening to `notification-activated`. On desktop the commands exist
and return neutral values.

#### This default permission set includes the following:

- `allow-has-all-files-access`
- `allow-request-all-files-access`
- `allow-notification-permission`
- `allow-request-notification-permission`
- `allow-can-schedule-exact`
- `allow-request-exact-alarms`
- `allow-schedule-reminders`
- `allow-take-activation`
- `allow-register-listener`
- `allow-remove-listener`

## Permission Table

<table>
<tr>
<th>Identifier</th>
<th>Description</th>
</tr>


<tr>
<td>

`brulion-android:allow-can-schedule-exact`

</td>
<td>

Enables the can_schedule_exact command without any pre-configured scope.

</td>
</tr>

<tr>
<td>

`brulion-android:deny-can-schedule-exact`

</td>
<td>

Denies the can_schedule_exact command without any pre-configured scope.

</td>
</tr>

<tr>
<td>

`brulion-android:allow-has-all-files-access`

</td>
<td>

Enables the has_all_files_access command without any pre-configured scope.

</td>
</tr>

<tr>
<td>

`brulion-android:deny-has-all-files-access`

</td>
<td>

Denies the has_all_files_access command without any pre-configured scope.

</td>
</tr>

<tr>
<td>

`brulion-android:allow-notification-permission`

</td>
<td>

Enables the notification_permission command without any pre-configured scope.

</td>
</tr>

<tr>
<td>

`brulion-android:deny-notification-permission`

</td>
<td>

Denies the notification_permission command without any pre-configured scope.

</td>
</tr>

<tr>
<td>

`brulion-android:allow-register-listener`

</td>
<td>

Enables the register_listener command without any pre-configured scope.

</td>
</tr>

<tr>
<td>

`brulion-android:deny-register-listener`

</td>
<td>

Denies the register_listener command without any pre-configured scope.

</td>
</tr>

<tr>
<td>

`brulion-android:allow-remove-listener`

</td>
<td>

Enables the remove_listener command without any pre-configured scope.

</td>
</tr>

<tr>
<td>

`brulion-android:deny-remove-listener`

</td>
<td>

Denies the remove_listener command without any pre-configured scope.

</td>
</tr>

<tr>
<td>

`brulion-android:allow-request-all-files-access`

</td>
<td>

Enables the request_all_files_access command without any pre-configured scope.

</td>
</tr>

<tr>
<td>

`brulion-android:deny-request-all-files-access`

</td>
<td>

Denies the request_all_files_access command without any pre-configured scope.

</td>
</tr>

<tr>
<td>

`brulion-android:allow-request-exact-alarms`

</td>
<td>

Enables the request_exact_alarms command without any pre-configured scope.

</td>
</tr>

<tr>
<td>

`brulion-android:deny-request-exact-alarms`

</td>
<td>

Denies the request_exact_alarms command without any pre-configured scope.

</td>
</tr>

<tr>
<td>

`brulion-android:allow-request-notification-permission`

</td>
<td>

Enables the request_notification_permission command without any pre-configured scope.

</td>
</tr>

<tr>
<td>

`brulion-android:deny-request-notification-permission`

</td>
<td>

Denies the request_notification_permission command without any pre-configured scope.

</td>
</tr>

<tr>
<td>

`brulion-android:allow-schedule-reminders`

</td>
<td>

Enables the schedule_reminders command without any pre-configured scope.

</td>
</tr>

<tr>
<td>

`brulion-android:deny-schedule-reminders`

</td>
<td>

Denies the schedule_reminders command without any pre-configured scope.

</td>
</tr>

<tr>
<td>

`brulion-android:allow-take-activation`

</td>
<td>

Enables the take_activation command without any pre-configured scope.

</td>
</tr>

<tr>
<td>

`brulion-android:deny-take-activation`

</td>
<td>

Denies the take_activation command without any pre-configured scope.

</td>
</tr>
</table>
