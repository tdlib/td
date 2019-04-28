double ? = Double;
string ? = String;

int32 = Int32;
int53 = Int53;
int64 = Int64;
bytes = Bytes;

boolFalse = Bool;
boolTrue = Bool;

vector {t:Type} # [ t ] = Vector t;


//@description An object of this type can be returned on every function call, in case of an error
//@code Error code; subject to future changes. If the error code is 406, the error message must not be processed in any way and must not be displayed to the user
//@message Error message; subject to future changes
error code:int32 message:string = Error;


//@description An object of this type is returned on a successful function call for certain functions
ok = Ok;


//@description Contains parameters for TDLib initialization
//@use_test_dc If set to true, the Telegram test environment will be used instead of the production environment
//@database_directory The path to the directory for the persistent database; if empty, the current working directory will be used
//@files_directory The path to the directory for storing files; if empty, database_directory will be used
//@use_file_database If set to true, information about downloaded and uploaded files will be saved between application restarts
//@use_chat_info_database If set to true, the library will maintain a cache of users, basic groups, supergroups, channels and secret chats. Implies use_file_database
//@use_message_database If set to true, the library will maintain a cache of chats and messages. Implies use_chat_info_database
//@use_secret_chats If set to true, support for secret chats will be enabled
//@api_id Application identifier for Telegram API access, which can be obtained at https://my.telegram.org
//@api_hash Application identifier hash for Telegram API access, which can be obtained at https://my.telegram.org
//@system_language_code IETF language tag of the user's operating system language; must be non-empty
//@device_model Model of the device the application is being run on; must be non-empty
//@system_version Version of the operating system the application is being run on; must be non-empty
//@application_version Application version; must be non-empty
//@enable_storage_optimizer If set to true, old files will automatically be deleted
//@ignore_file_names If set to true, original file names will be ignored. Otherwise, downloaded files will be saved under names as close as possible to the original name
tdlibParameters use_test_dc:Bool database_directory:string files_directory:string use_file_database:Bool use_chat_info_database:Bool use_message_database:Bool use_secret_chats:Bool api_id:int32 api_hash:string system_language_code:string device_model:string system_version:string application_version:string enable_storage_optimizer:Bool ignore_file_names:Bool = TdlibParameters;


//@class AuthenticationCodeType @description Provides information about the method by which an authentication code is delivered to the user

//@description An authentication code is delivered via a private Telegram message, which can be viewed in another client @length Length of the code
authenticationCodeTypeTelegramMessage length:int32 = AuthenticationCodeType;

//@description An authentication code is delivered via an SMS message to the specified phone number @length Length of the code
authenticationCodeTypeSms length:int32 = AuthenticationCodeType;

//@description An authentication code is delivered via a phone call to the specified phone number @length Length of the code
authenticationCodeTypeCall length:int32 = AuthenticationCodeType;

//@description An authentication code is delivered by an immediately cancelled call to the specified phone number. The number from which the call was made is the code @pattern Pattern of the phone number from which the call will be made
authenticationCodeTypeFlashCall pattern:string = AuthenticationCodeType;


//@description Information about the authentication code that was sent @phone_number A phone number that is being authenticated @type Describes the way the code was sent to the user @next_type Describes the way the next code will be sent to the user; may be null @timeout Timeout before the code should be re-sent, in seconds
authenticationCodeInfo phone_number:string type:AuthenticationCodeType next_type:AuthenticationCodeType timeout:int32 = AuthenticationCodeInfo;

//@description Information about the email address authentication code that was sent @email_address_pattern Pattern of the email address to which an authentication code was sent @length Length of the code; 0 if unknown
emailAddressAuthenticationCodeInfo email_address_pattern:string length:int32 = EmailAddressAuthenticationCodeInfo;


//@description Represents a part of the text that needs to be formatted in some unusual way @offset Offset of the entity in UTF-16 code points @length Length of the entity, in UTF-16 code points @type Type of the entity
textEntity offset:int32 length:int32 type:TextEntityType = TextEntity;

//@description Contains a list of text entities @entities List of text entities
textEntities entities:vector<textEntity> = TextEntities;

//@description A text with some entities @text The text @entities Entities contained in the text
formattedText text:string entities:vector<textEntity> = FormattedText;


//@description Contains Telegram terms of service @text Text of the terms of service @min_user_age Mininum age of a user to be able to accept the terms; 0 if any @show_popup True, if a blocking popup with terms of service must be shown to the user
termsOfService text:formattedText min_user_age:int32 show_popup:Bool = TermsOfService;


//@class AuthorizationState @description Represents the current authorization state of the client

//@description TDLib needs TdlibParameters for initialization
authorizationStateWaitTdlibParameters = AuthorizationState;

//@description TDLib needs an encryption key to decrypt the local database @is_encrypted True, if the database is currently encrypted
authorizationStateWaitEncryptionKey is_encrypted:Bool = AuthorizationState;

//@description TDLib needs the user's phone number to authorize
authorizationStateWaitPhoneNumber = AuthorizationState;

//@description TDLib needs the user's authentication code to finalize authorization @is_registered True, if the user is already registered @terms_of_service Telegram terms of service, which should be accepted before user can continue registration; may be null @code_info Information about the authorization code that was sent
authorizationStateWaitCode is_registered:Bool terms_of_service:termsOfService code_info:authenticationCodeInfo = AuthorizationState;

//@description The user has been authorized, but needs to enter a password to start using the application @password_hint Hint for the password; may be empty @has_recovery_email_address True if a recovery email address has been set up
//@recovery_email_address_pattern Pattern of the email address to which the recovery email was sent; empty until a recovery email has been sent
authorizationStateWaitPassword password_hint:string has_recovery_email_address:Bool recovery_email_address_pattern:string = AuthorizationState;

//@description The user has been successfully authorized. TDLib is now ready to answer queries
authorizationStateReady = AuthorizationState;

//@description The user is currently logging out
authorizationStateLoggingOut = AuthorizationState;

//@description TDLib is closing, all subsequent queries will be answered with the error 500. Note that closing TDLib can take a while. All resources will be freed only after authorizationStateClosed has been received
authorizationStateClosing = AuthorizationState;

//@description TDLib client is in its final state. All databases are closed and all resources are released. No other updates will be received after this. All queries will be responded to
//-with error code 500. To continue working, one should create a new instance of the TDLib client
authorizationStateClosed = AuthorizationState;


//@description Represents the current state of 2-step verification @has_password True, if a 2-step verification password is set @password_hint Hint for the password; may be empty
//@has_recovery_email_address True, if a recovery email is set @has_passport_data True, if some Telegram Passport elements were saved
//@recovery_email_address_code_info Information about the recovery email address to which the confirmation email was sent; may be null
passwordState has_password:Bool password_hint:string has_recovery_email_address:Bool has_passport_data:Bool recovery_email_address_code_info:emailAddressAuthenticationCodeInfo = PasswordState;

//@description Contains information about the current recovery email address @recovery_email_address Recovery email address
recoveryEmailAddress recovery_email_address:string = RecoveryEmailAddress;


//@description Returns information about the availability of a temporary password, which can be used for payments @has_password True, if a temporary password is available @valid_for Time left before the temporary password expires, in seconds
temporaryPasswordState has_password:Bool valid_for:int32 = TemporaryPasswordState;


//@description Represents a local file
//@path Local path to the locally available file part; may be empty
//@can_be_downloaded True, if it is possible to try to download or generate the file
//@can_be_deleted True, if the file can be deleted
//@is_downloading_active True, if the file is currently being downloaded (or a local copy is being generated by some other means)
//@is_downloading_completed True, if the local copy is fully available
//@download_offset Download will be started from this offset. downloaded_prefix_size is calculated from this offset
//@downloaded_prefix_size If is_downloading_completed is false, then only some prefix of the file starting from download_offset is ready to be read. downloaded_prefix_size is the size of that prefix
//@downloaded_size Total downloaded file bytes. Should be used only for calculating download progress. The actual file size may be bigger, and some parts of it may contain garbage
localFile path:string can_be_downloaded:Bool can_be_deleted:Bool is_downloading_active:Bool is_downloading_completed:Bool download_offset:int32 downloaded_prefix_size:int32 downloaded_size:int32 = LocalFile;

//@description Represents a remote file
//@id Remote file identifier; may be empty. Can be used across application restarts or even from other devices for the current user. If the ID starts with "http://" or "https://", it represents the HTTP URL of the file. TDLib is currently unable to download files if only their URL is known.
//-If downloadFile is called on such a file or if it is sent to a secret chat, TDLib starts a file generation process by sending updateFileGenerationStart to the client with the HTTP URL in the original_path and "#url#" as the conversion string. Clients should generate the file by downloading it to the specified location
//@is_uploading_active True, if the file is currently being uploaded (or a remote copy is being generated by some other means)
//@is_uploading_completed True, if a remote copy is fully available
//@uploaded_size Size of the remote available part of the file; 0 if unknown
remoteFile id:string is_uploading_active:Bool is_uploading_completed:Bool uploaded_size:int32 = RemoteFile;

//@description Represents a file
//@id Unique file identifier
//@size File size; 0 if unknown
//@expected_size Expected file size in case the exact file size is unknown, but an approximate size is known. Can be used to show download/upload progress
//@local Information about the local copy of the file
//@remote Information about the remote copy of the file
file id:int32 size:int32 expected_size:int32 local:localFile remote:remoteFile = File;


//@class InputFile @description Points to a file

//@description A file defined by its unique ID @id Unique file identifier
inputFileId id:int32 = InputFile;

//@description A file defined by its remote ID @id Remote file identifier
inputFileRemote id:string = InputFile;

//@description A file defined by a local path @path Local path to the file
inputFileLocal path:string = InputFile;

//@description A file generated by the client @original_path Local path to a file from which the file is generated; may be empty if there is no such file
//@conversion String specifying the conversion applied to the original file; should be persistent across application restarts. Conversions beginning with '#' are reserved for internal TDLib usage
//@expected_size Expected size of the generated file; 0 if unknown
inputFileGenerated original_path:string conversion:string expected_size:int32 = InputFile;


//@description Photo description @type Thumbnail type (see https://core.telegram.org/constructor/photoSize) @photo Information about the photo file @width Photo width @height Photo height
photoSize type:string photo:file width:int32 height:int32 = PhotoSize;


//@class MaskPoint @description Part of the face, relative to which a mask should be placed

//@description A mask should be placed relatively to the forehead
maskPointForehead = MaskPoint;

//@description A mask should be placed relatively to the eyes
maskPointEyes = MaskPoint;

//@description A mask should be placed relatively to the mouth
maskPointMouth = MaskPoint;

//@description A mask should be placed relatively to the chin
maskPointChin = MaskPoint;

//@description Position on a photo where a mask should be placed @point Part of the face, relative to which the mask should be placed
//@x_shift Shift by X-axis measured in widths of the mask scaled to the face size, from left to right. (For example, -1.0 will place the mask just to the left of the default mask position)
//@y_shift Shift by Y-axis measured in heights of the mask scaled to the face size, from top to bottom. (For example, 1.0 will place the mask just below the default mask position)
//@scale Mask scaling coefficient. (For example, 2.0 means a doubled size)
maskPosition point:MaskPoint x_shift:double y_shift:double scale:double = MaskPosition;


//@description Describes one answer option of a poll @text Option text, 1-100 characters @voter_count Number of voters for this option, available only for closed or voted polls @vote_percentage The percentage of votes for this option, 0-100
//@is_chosen True, if the option was chosen by the user @is_being_chosen True, if the option is being chosen by a pending setPollAnswer request
pollOption text:string voter_count:int32 vote_percentage:int32 is_chosen:Bool is_being_chosen:Bool = PollOption;


//@description Describes an animation file. The animation must be encoded in GIF or MPEG4 format @duration Duration of the animation, in seconds; as defined by the sender @width Width of the animation @height Height of the animation
//@file_name Original name of the file; as defined by the sender @mime_type MIME type of the file, usually "image/gif" or "video/mp4" @thumbnail Animation thumbnail; may be null @animation File containing the animation
animation duration:int32 width:int32 height:int32 file_name:string mime_type:string thumbnail:photoSize animation:file = Animation;

//@description Describes an audio file. Audio is usually in MP3 format @duration Duration of the audio, in seconds; as defined by the sender @title Title of the audio; as defined by the sender @performer Performer of the audio; as defined by the sender
//@file_name Original name of the file; as defined by the sender @mime_type The MIME type of the file; as defined by the sender @album_cover_thumbnail The thumbnail of the album cover; as defined by the sender. The full size thumbnail should be extracted from the downloaded file; may be null @audio File containing the audio
audio duration:int32 title:string performer:string file_name:string mime_type:string album_cover_thumbnail:photoSize audio:file = Audio;

//@description Describes a document of any type @file_name Original name of the file; as defined by the sender @mime_type MIME type of the file; as defined by the sender
//@thumbnail Document thumbnail; as defined by the sender; may be null @document File containing the document
document file_name:string mime_type:string thumbnail:photoSize document:file = Document;

//@description Describes a photo @has_stickers True, if stickers were added to the photo @sizes Available variants of the photo, in different sizes
photo has_stickers:Bool sizes:vector<photoSize> = Photo;

//@description Describes a sticker @set_id The identifier of the sticker set to which the sticker belongs; 0 if none @width Sticker width; as defined by the sender @height Sticker height; as defined by the sender
//@emoji Emoji corresponding to the sticker @is_mask True, if the sticker is a mask @mask_position Position where the mask should be placed; may be null @thumbnail Sticker thumbnail in WEBP or JPEG format; may be null @sticker File containing the sticker
sticker set_id:int64 width:int32 height:int32 emoji:string is_mask:Bool mask_position:maskPosition thumbnail:photoSize sticker:file = Sticker;

//@description Describes a video file @duration Duration of the video, in seconds; as defined by the sender @width Video width; as defined by the sender @height Video height; as defined by the sender
//@file_name Original name of the file; as defined by the sender @mime_type MIME type of the file; as defined by the sender @has_stickers True, if stickers were added to the photo
//@supports_streaming True, if the video should be tried to be streamed @thumbnail Video thumbnail; as defined by the sender; may be null @video File containing the video
video duration:int32 width:int32 height:int32 file_name:string mime_type:string has_stickers:Bool supports_streaming:Bool thumbnail:photoSize video:file = Video;

//@description Describes a video note. The video must be equal in width and height, cropped to a circle, and stored in MPEG4 format @duration Duration of the video, in seconds; as defined by the sender @length Video width and height; as defined by the sender @thumbnail Video thumbnail; as defined by the sender; may be null @video File containing the video
videoNote duration:int32 length:int32 thumbnail:photoSize video:file = VideoNote;

//@description Describes a voice note. The voice note must be encoded with the Opus codec, and stored inside an OGG container. Voice notes can have only a single audio channel @duration Duration of the voice note, in seconds; as defined by the sender
//@waveform A waveform representation of the voice note in 5-bit format @mime_type MIME type of the file; as defined by the sender @voice File containing the voice note
voiceNote duration:int32 waveform:bytes mime_type:string voice:file = VoiceNote;

//@description Describes a user contact @phone_number Phone number of the user @first_name First name of the user; 1-255 characters in length @last_name Last name of the user @vcard Additional data about the user in a form of vCard; 0-2048 bytes in length @user_id Identifier of the user, if known; otherwise 0
contact phone_number:string first_name:string last_name:string vcard:string user_id:int32 = Contact;

//@description Describes a location on planet Earth @latitude Latitude of the location in degrees; as defined by the sender @longitude Longitude of the location, in degrees; as defined by the sender
location latitude:double longitude:double = Location;

//@description Describes a venue @location Venue location; as defined by the sender @title Venue name; as defined by the sender @address Venue address; as defined by the sender @provider Provider of the venue database; as defined by the sender. Currently only "foursquare" needs to be supported
//@id Identifier of the venue in the provider database; as defined by the sender @type Type of the venue in the provider database; as defined by the sender
venue location:location title:string address:string provider:string id:string type:string = Venue;

//@description Describes a game @id Game ID @short_name Game short name. To share a game use the URL https://t.me/{bot_username}?game={game_short_name} @title Game title @text Game text, usually containing scoreboards for a game
//@param_description Game description @photo Game photo @animation Game animation; may be null
game id:int64 short_name:string title:string text:formattedText description:string photo:photo animation:animation = Game;

//@description Describes a poll @id Unique poll identifier @question Poll question, 1-255 characters @options List of poll answer options @total_voter_count Total number of voters, participating in the poll @is_closed True, if the poll is closed
poll id:int64 question:string options:vector<pollOption> total_voter_count:int32 is_closed:Bool = Poll;


//@description Describes a user profile photo @id Photo identifier; 0 for an empty photo. Can be used to find a photo in a list of userProfilePhotos
//@small A small (160x160) user profile photo @big A big (640x640) user profile photo
profilePhoto id:int64 small:file big:file = ProfilePhoto;

//@description Describes the photo of a chat @small A small (160x160) chat photo @big A big (640x640) chat photo
chatPhoto small:file big:file = ChatPhoto;


//@class LinkState @description Represents the relationship between user A and user B. For incoming_link, user A is the current user; for outgoing_link, user B is the current user

//@description The phone number of user A is not known to user B
linkStateNone = LinkState;

//@description The phone number of user A is known but that number has not been saved to the contact list of user B
linkStateKnowsPhoneNumber = LinkState;

//@description The phone number of user A has been saved to the contact list of user B
linkStateIsContact = LinkState;


//@class UserType @description Represents the type of the user. The following types are possible: regular users, deleted users and bots

//@description A regular user
userTypeRegular = UserType;

//@description A deleted user or deleted bot. No information on the user besides the user_id is available. It is not possible to perform any active actions on this type of user
userTypeDeleted = UserType;

//@description A bot (see https://core.telegram.org/bots) @can_join_groups True, if the bot can be invited to basic group and supergroup chats
//@can_read_all_group_messages True, if the bot can read all messages in basic group or supergroup chats and not just those addressed to the bot. In private and channel chats a bot can always read all messages
//@is_inline True, if the bot supports inline queries @inline_query_placeholder Placeholder for inline queries (displayed on the client input field) @need_location True, if the location of the user should be sent with every inline query to this bot
userTypeBot can_join_groups:Bool can_read_all_group_messages:Bool is_inline:Bool inline_query_placeholder:string need_location:Bool = UserType;

//@description No information on the user besides the user_id is available, yet this user has not been deleted. This object is extremely rare and must be handled like a deleted user. It is not possible to perform any actions on users of this type
userTypeUnknown = UserType;


//@description Represents commands supported by a bot @command Text of the bot command @param_description Description of the bot command
botCommand command:string description:string = BotCommand;

//@description Provides information about a bot and its supported commands @param_description Long description shown on the user info page @commands A list of commands supported by the bot
botInfo description:string commands:vector<botCommand> = BotInfo;


//@description Represents a user @id User identifier @first_name First name of the user @last_name Last name of the user @username Username of the user
//@phone_number Phone number of the user @status Current online status of the user @profile_photo Profile photo of the user; may be null
//@outgoing_link Relationship from the current user to the other user @incoming_link Relationship from the other user to the current user
//@is_verified True, if the user is verified @is_support True, if the user is Telegram support account
//@restriction_reason If non-empty, it contains the reason why access to this user must be restricted. The format of the string is "{type}: {description}".
//-{type} contains the type of the restriction and at least one of the suffixes "-all", "-ios", "-android", or "-wp", which describe the platforms on which access should be restricted. (For example, "terms-ios-android". {description} contains a human-readable description of the restriction, which can be shown to the user)
//@have_access If false, the user is inaccessible, and the only information known about the user is inside this class. It can't be passed to any method except GetUser @type Type of the user @language_code IETF language tag of the user's language; only available to bots
user id:int32 first_name:string last_name:string username:string phone_number:string status:UserStatus profile_photo:profilePhoto outgoing_link:LinkState incoming_link:LinkState is_verified:Bool is_support:Bool restriction_reason:string have_access:Bool type:UserType language_code:string = User;

//@description Contains full information about a user (except the full list of profile photos) @is_blocked True, if the user is blacklisted by the current user @can_be_called True, if the user can be called @has_private_calls True, if the user can't be called due to their privacy settings
//@bio A short user bio @share_text For bots, the text that is included with the link when users share the bot @group_in_common_count Number of group chats where both the other user and the current user are a member; 0 for the current user @bot_info If the user is a bot, information about the bot; may be null
userFullInfo is_blocked:Bool can_be_called:Bool has_private_calls:Bool bio:string share_text:string group_in_common_count:int32 bot_info:botInfo = UserFullInfo;

//@description Contains full information about a user profile photo @id Unique user profile photo identifier @added_date Point in time (Unix timestamp) when the photo has been added @sizes Available variants of the user photo, in different sizes
userProfilePhoto id:int64 added_date:int32 sizes:vector<photoSize> = UserProfilePhoto;

//@description Contains part of the list of user photos @total_count Total number of user profile photos @photos A list of photos
userProfilePhotos total_count:int32 photos:vector<userProfilePhoto> = UserProfilePhotos;

//@description Represents a list of users @total_count Approximate total count of users found @user_ids A list of user identifiers
users total_count:int32 user_ids:vector<int32> = Users;


//@class ChatMemberStatus @description Provides information about the status of a member in a chat

//@description The user is the creator of a chat and has all the administrator privileges @is_member True, if the user is a member of the chat
chatMemberStatusCreator is_member:Bool = ChatMemberStatus;

//@description The user is a member of a chat and has some additional privileges. In basic groups, administrators can edit and delete messages sent by others, add new members, and ban unprivileged members. In supergroups and channels, there are more detailed options for administrator privileges
//@can_be_edited True, if the current user can edit the administrator privileges for the called user
//@can_change_info True, if the administrator can change the chat title, photo, and other settings
//@can_post_messages True, if the administrator can create channel posts; applicable to channels only
//@can_edit_messages True, if the administrator can edit messages of other users and pin messages; applicable to channels only
//@can_delete_messages True, if the administrator can delete messages of other users
//@can_invite_users True, if the administrator can invite new users to the chat
//@can_restrict_members True, if the administrator can restrict, ban, or unban chat members
//@can_pin_messages True, if the administrator can pin messages; applicable to groups only
//@can_promote_members True, if the administrator can add new administrators with a subset of his own privileges or demote administrators that were directly or indirectly promoted by him
chatMemberStatusAdministrator can_be_edited:Bool can_change_info:Bool can_post_messages:Bool can_edit_messages:Bool can_delete_messages:Bool can_invite_users:Bool can_restrict_members:Bool can_pin_messages:Bool can_promote_members:Bool = ChatMemberStatus;

//@description The user is a member of a chat, without any additional privileges or restrictions
chatMemberStatusMember = ChatMemberStatus;

//@description The user is under certain restrictions in the chat. Not supported in basic groups and channels
//@is_member True, if the user is a member of the chat
//@restricted_until_date Point in time (Unix timestamp) when restrictions will be lifted from the user; 0 if never. If the user is restricted for more than 366 days or for less than 30 seconds from the current time, the user is considered to be restricted forever
//@can_send_messages True, if the user can send text messages, contacts, locations, and venues
//@can_send_media_messages True, if the user can send audio files, documents, photos, videos, video notes, and voice notes. Implies can_send_messages permissions
//@can_send_other_messages True, if the user can send animations, games, and stickers and use inline bots. Implies can_send_media_messages permissions
//@can_add_web_page_previews True, if the user may add a web page preview to his messages. Implies can_send_messages permissions
chatMemberStatusRestricted is_member:Bool restricted_until_date:int32 can_send_messages:Bool can_send_media_messages:Bool can_send_other_messages:Bool can_add_web_page_previews:Bool = ChatMemberStatus;

//@description The user is not a chat member
chatMemberStatusLeft = ChatMemberStatus;

//@description The user was banned (and hence is not a member of the chat). Implies the user can't return to the chat or view messages
//@banned_until_date Point in time (Unix timestamp) when the user will be unbanned; 0 if never. If the user is banned for more than 366 days or for less than 30 seconds from the current time, the user is considered to be banned forever
chatMemberStatusBanned banned_until_date:int32 = ChatMemberStatus;


//@description A user with information about joining/leaving a chat @user_id User identifier of the chat member @inviter_user_id Identifier of a user that invited/promoted/banned this member in the chat; 0 if unknown
//@joined_chat_date Point in time (Unix timestamp) when the user joined a chat @status Status of the member in the chat @bot_info If the user is a bot, information about the bot; may be null. Can be null even for a bot if the bot is not a chat member
chatMember user_id:int32 inviter_user_id:int32 joined_chat_date:int32 status:ChatMemberStatus bot_info:botInfo = ChatMember;

//@description Contains a list of chat members @total_count Approximate total count of chat members found @members A list of chat members
chatMembers total_count:int32 members:vector<chatMember> = ChatMembers;


//@class ChatMembersFilter @description Specifies the kind of chat members to return in searchChatMembers

//@description Returns the creator and administrators
chatMembersFilterAdministrators = ChatMembersFilter;

//@description Returns all chat members, including restricted chat members
chatMembersFilterMembers = ChatMembersFilter;

//@description Returns users under certain restrictions in the chat; can be used only by administrators in a supergroup
chatMembersFilterRestricted = ChatMembersFilter;

//@description Returns users banned from the chat; can be used only by administrators in a supergroup or in a channel
chatMembersFilterBanned = ChatMembersFilter;

//@description Returns bot members of the chat
chatMembersFilterBots = ChatMembersFilter;


//@class SupergroupMembersFilter @description Specifies the kind of chat members to return in getSupergroupMembers

//@description Returns recently active users in reverse chronological order
supergroupMembersFilterRecent = SupergroupMembersFilter;

//@description Returns the creator and administrators
supergroupMembersFilterAdministrators = SupergroupMembersFilter;

//@description Used to search for supergroup or channel members via a (string) query @query Query to search for
supergroupMembersFilterSearch query:string = SupergroupMembersFilter;

//@description Returns restricted supergroup members; can be used only by administrators @query Query to search for
supergroupMembersFilterRestricted query:string = SupergroupMembersFilter;

//@description Returns users banned from the supergroup or channel; can be used only by administrators @query Query to search for
supergroupMembersFilterBanned query:string = SupergroupMembersFilter;

//@description Returns bot members of the supergroup or channel
supergroupMembersFilterBots = SupergroupMembersFilter;


//@description Represents a basic group of 0-200 users (must be upgraded to a supergroup to accommodate more than 200 users)
//@id Group identifier
//@member_count Number of members in the group
//@status Status of the current user in the group
//@everyone_is_administrator True, if all members have been granted administrator rights in the group
//@is_active True, if the group is active
//@upgraded_to_supergroup_id Identifier of the supergroup to which this group was upgraded; 0 if none
basicGroup id:int32 member_count:int32 status:ChatMemberStatus everyone_is_administrator:Bool is_active:Bool upgraded_to_supergroup_id:int32 = BasicGroup;

//@description Contains full information about a basic group @creator_user_id User identifier of the creator of the group; 0 if unknown @members Group members @invite_link Invite link for this group; available only for the group creator and only after it has been generated at least once
basicGroupFullInfo creator_user_id:int32 members:vector<chatMember> invite_link:string = BasicGroupFullInfo;


//@description Represents a supergroup or channel with zero or more members (subscribers in the case of channels). From the point of view of the system, a channel is a special kind of a supergroup: only administrators can post and see the list of members, and posts from all administrators use the name and photo of the channel instead of individual names and profile photos. Unlike supergroups, channels can have an unlimited number of subscribers
//@id Supergroup or channel identifier
//@username Username of the supergroup or channel; empty for private supergroups or channels
//@date Point in time (Unix timestamp) when the current user joined, or the point in time when the supergroup or channel was created, in case the user is not a member
//@status Status of the current user in the supergroup or channel
//@member_count Member count; 0 if unknown. Currently it is guaranteed to be known only if the supergroup or channel was found through SearchPublicChats
//@anyone_can_invite True, if any member of the supergroup can invite other members. This field has no meaning for channels
//@sign_messages True, if messages sent to the channel should contain information about the sender. This field is only applicable to channels
//@is_channel True, if the supergroup is a channel
//@is_verified True, if the supergroup or channel is verified
//@restriction_reason If non-empty, contains the reason why access to this supergroup or channel must be restricted. Format of the string is "{type}: {description}".
//-{type} Contains the type of the restriction and at least one of the suffixes "-all", "-ios", "-android", or "-wp", which describe the platforms on which access should be restricted. (For example, "terms-ios-android". {description} contains a human-readable description of the restriction, which can be shown to the user)
supergroup id:int32 username:string date:int32 status:ChatMemberStatus member_count:int32 anyone_can_invite:Bool sign_messages:Bool is_channel:Bool is_verified:Bool restriction_reason:string = Supergroup;

//@description Contains full information about a supergroup or channel
//@param_description Supergroup or channel description
//@member_count Number of members in the supergroup or channel; 0 if unknown
//@administrator_count Number of privileged users in the supergroup or channel; 0 if unknown
//@restricted_count Number of restricted users in the supergroup; 0 if unknown
//@banned_count Number of users banned from chat; 0 if unknown
//@can_get_members True, if members of the chat can be retrieved
//@can_set_username True, if the chat can be made public
//@can_set_sticker_set True, if the supergroup sticker set can be changed
//@can_view_statistics True, if the channel statistics is available through getChatStatisticsUrl
//@is_all_history_available True, if new chat members will have access to old messages. In public supergroups and both public and private channels, old messages are always available, so this option affects only private supergroups. The value of this field is only available for chat administrators
//@sticker_set_id Identifier of the supergroup sticker set; 0 if none
//@invite_link Invite link for this chat
//@upgraded_from_basic_group_id Identifier of the basic group from which supergroup was upgraded; 0 if none
//@upgraded_from_max_message_id Identifier of the last message in the basic group from which supergroup was upgraded; 0 if none
supergroupFullInfo description:string member_count:int32 administrator_count:int32 restricted_count:int32 banned_count:int32 can_get_members:Bool can_set_username:Bool can_set_sticker_set:Bool can_view_statistics:Bool is_all_history_available:Bool sticker_set_id:int64 invite_link:string upgraded_from_basic_group_id:int32 upgraded_from_max_message_id:int53 = SupergroupFullInfo;


//@class SecretChatState @description Describes the current secret chat state

//@description The secret chat is not yet created; waiting for the other user to get online
secretChatStatePending = SecretChatState;

//@description The secret chat is ready to use
secretChatStateReady = SecretChatState;

//@description The secret chat is closed
secretChatStateClosed = SecretChatState;


//@description Represents a secret chat
//@id Secret chat identifier
//@user_id Identifier of the chat partner
//@state State of the secret chat
//@is_outbound True, if the chat was created by the current user; otherwise false
//@ttl Current message Time To Live setting (self-destruct timer) for the chat, in seconds
//@key_hash Hash of the currently used key for comparison with the hash of the chat partner's key. This is a string of 36 bytes, which must be used to make a 12x12 square image with a color depth of 4. The first 16 bytes should be used to make a central 8x8 square, while the remaining 20 bytes should be used to construct a 2-pixel-wide border around that square.
//-Alternatively, the first 32 bytes of the hash can be converted to the hexadecimal format and printed as 32 2-digit hex numbers
//@layer Secret chat layer; determines features supported by the other client. Video notes are supported if the layer >= 66
secretChat id:int32 user_id:int32 state:SecretChatState is_outbound:Bool ttl:int32 key_hash:bytes layer:int32 = SecretChat;


//@class MessageForwardOrigin @description Contains information about the origin of a forwarded message

//@description The message was originally written by a known user @sender_user_id Identifier of the user that originally sent the message
messageForwardOriginUser sender_user_id:int32 = MessageForwardOrigin;

//@description The message was originally written by a user, which is hidden by his privacy settings @sender_name Name of the sender
messageForwardOriginHiddenUser sender_name:string = MessageForwardOrigin;

//@description The message was originally a post in a channel
//@chat_id Identifier of the chat from which the message was originally forwarded
//@message_id Message identifier of the original message; 0 if unknown
//@author_signature Original post author signature
messageForwardOriginChannel chat_id:int53 message_id:int53 author_signature:string = MessageForwardOrigin;


//@description Contains information about a forwarded message
//@origin Origin of a forwarded message
//@date Point in time (Unix timestamp) when the message was originally sent
//@from_chat_id For messages forwarded to the chat with the current user (saved messages), the identifier of the chat from which the message was forwarded last time; 0 if unknown
//@from_message_id For messages forwarded to the chat with the current user (saved messages), the identifier of the original message from which the new message was forwarded last time; 0 if unknown
messageForwardInfo origin:MessageForwardOrigin date:int32 from_chat_id:int53 from_message_id:int53 = MessageForwardInfo;


//@class MessageSendingState @description Contains information about the sending state of the message

//@description The message is being sent now, but has not yet been delivered to the server
messageSendingStatePending = MessageSendingState;

//@description The message failed to be sent
messageSendingStateFailed = MessageSendingState;


//@description Describes a message
//@id Message identifier, unique for the chat to which the message belongs
//@sender_user_id Identifier of the user who sent the message; 0 if unknown. It is unknown for channel posts
//@chat_id Chat identifier
//@sending_state Information about the sending state of the message; may be null
//@is_outgoing True, if the message is outgoing
//@can_be_edited True, if the message can be edited. For live location and poll messages this fields shows, whether editMessageLiveLocation or stopPoll can be used with this message by the client
//@can_be_forwarded True, if the message can be forwarded
//@can_be_deleted_only_for_self True, if the message can be deleted only for the current user while other users will continue to see it
//@can_be_deleted_for_all_users True, if the message can be deleted for all users
//@is_channel_post True, if the message is a channel post. All messages to channels are channel posts, all other messages are not channel posts
//@contains_unread_mention True, if the message contains an unread mention for the current user
//@date Point in time (Unix timestamp) when the message was sent
//@edit_date Point in time (Unix timestamp) when the message was last edited
//@forward_info Information about the initial message sender; may be null
//@reply_to_message_id If non-zero, the identifier of the message this message is replying to; can be the identifier of a deleted message
//@ttl For self-destructing messages, the message's TTL (Time To Live), in seconds; 0 if none. TDLib will send updateDeleteMessages or updateMessageContent once the TTL expires
//@ttl_expires_in Time left before the message expires, in seconds
//@via_bot_user_id If non-zero, the user identifier of the bot through which this message was sent
//@author_signature For channel posts, optional author signature
//@views Number of times this message was viewed
//@media_album_id Unique identifier of an album this message belongs to. Only photos and videos can be grouped together in albums
//@content Content of the message
//@reply_markup Reply markup for the message; may be null
message id:int53 sender_user_id:int32 chat_id:int53 sending_state:MessageSendingState is_outgoing:Bool can_be_edited:Bool can_be_forwarded:Bool can_be_deleted_only_for_self:Bool can_be_deleted_for_all_users:Bool is_channel_post:Bool contains_unread_mention:Bool date:int32 edit_date:int32 forward_info:messageForwardInfo reply_to_message_id:int53 ttl:int32 ttl_expires_in:double via_bot_user_id:int32 author_signature:string views:int32 media_album_id:int64 content:MessageContent reply_markup:ReplyMarkup = Message;

//@description Contains a list of messages @total_count Approximate total count of messages found @messages List of messages; messages may be null
messages total_count:int32 messages:vector<message> = Messages;

//@description Contains a list of messages found by a search @messages List of messages @next_from_search_id Value to pass as from_search_id to get more results
foundMessages messages:vector<message> next_from_search_id:int64 = FoundMessages;


//@class NotificationSettingsScope @description Describes the types of chats to which notification settings are applied

//@description Notification settings applied to all private and secret chats when the corresponding chat setting has a default value
notificationSettingsScopePrivateChats = NotificationSettingsScope;

//@description Notification settings applied to all basic groups and supergroups when the corresponding chat setting has a default value
notificationSettingsScopeGroupChats = NotificationSettingsScope;

//@description Notification settings applied to all channels when the corresponding chat setting has a default value
notificationSettingsScopeChannelChats = NotificationSettingsScope;


//@description Contains information about notification settings for a chat
//@use_default_mute_for If true, mute_for is ignored and the value for the relevant type of chat is used instead @mute_for Time left before notifications will be unmuted, in seconds
//@use_default_sound If true, sound is ignored and the value for the relevant type of chat is used instead @sound The name of an audio file to be used for notification sounds; only applies to iOS applications
//@use_default_show_preview If true, show_preview is ignored and the value for the relevant type of chat is used instead @show_preview True, if message content should be displayed in notifications
//@use_default_disable_pinned_message_notifications If true, disable_pinned_message_notifications is ignored and the value for the relevant type of chat is used instead @disable_pinned_message_notifications If true, notifications for incoming pinned messages will be created as for an ordinary unread message
//@use_default_disable_mention_notifications If true, disable_mention_notifications is ignored and the value for the relevant type of chat is used instead @disable_mention_notifications If true, notifications for messages with mentions will be created as for an ordinary unread message
chatNotificationSettings use_default_mute_for:Bool mute_for:int32 use_default_sound:Bool sound:string use_default_show_preview:Bool show_preview:Bool use_default_disable_pinned_message_notifications:Bool disable_pinned_message_notifications:Bool use_default_disable_mention_notifications:Bool disable_mention_notifications:Bool = ChatNotificationSettings;

//@description Contains information about notification settings for several chats
//@mute_for Time left before notifications will be unmuted, in seconds
//@sound The name of an audio file to be used for notification sounds; only applies to iOS applications
//@show_preview True, if message content should be displayed in notifications
//@disable_pinned_message_notifications True, if notifications for incoming pinned messages will be created as for an ordinary unread message
//@disable_mention_notifications True, if notifications for messages with mentions will be created as for an ordinary unread message
scopeNotificationSettings mute_for:int32 sound:string show_preview:Bool disable_pinned_message_notifications:Bool disable_mention_notifications:Bool = ScopeNotificationSettings;


//@description Contains information about a message draft @reply_to_message_id Identifier of the message to reply to; 0 if none @input_message_text Content of the message draft; this should always be of type inputMessageText
draftMessage reply_to_message_id:int53 input_message_text:InputMessageContent = DraftMessage;


//@class ChatType @description Describes the type of a chat

//@description An ordinary chat with a user @user_id User identifier
chatTypePrivate user_id:int32 = ChatType;

//@description A basic group (i.e., a chat with 0-200 other users) @basic_group_id Basic group identifier
chatTypeBasicGroup basic_group_id:int32 = ChatType;

//@description A supergroup (i.e. a chat with up to GetOption("supergroup_max_size") other users), or channel (with unlimited members) @supergroup_id Supergroup or channel identifier @is_channel True, if the supergroup is a channel
chatTypeSupergroup supergroup_id:int32 is_channel:Bool = ChatType;

//@description A secret chat with a user @secret_chat_id Secret chat identifier @user_id User identifier of the secret chat peer
chatTypeSecret secret_chat_id:int32 user_id:int32 = ChatType;


//@description A chat. (Can be a private chat, basic group, supergroup, or secret chat)
//@id Chat unique identifier
//@type Type of the chat
//@title Chat title
//@photo Chat photo; may be null
//@last_message Last message in the chat; may be null
//@order Descending parameter by which chats are sorted in the main chat list. If the order number of two chats is the same, they must be sorted in descending order by ID. If 0, the position of the chat in the list is undetermined
//@is_pinned True, if the chat is pinned
//@is_marked_as_unread True, if the chat is marked as unread
//@is_sponsored True, if the chat is sponsored by the user's MTProxy server
//@can_be_deleted_only_for_self True, if the chat messages can be deleted only for the current user while other users will continue to see the messages
//@can_be_deleted_for_all_users True, if the chat messages can be deleted for all users
//@can_be_reported True, if the chat can be reported to Telegram moderators through reportChat
//@default_disable_notification Default value of the disable_notification parameter, used when a message is sent to the chat
//@unread_count Number of unread messages in the chat
//@last_read_inbox_message_id Identifier of the last read incoming message
//@last_read_outbox_message_id Identifier of the last read outgoing message
//@unread_mention_count Number of unread messages with a mention/reply in the chat
//@notification_settings Notification settings for this chat
//@pinned_message_id Identifier of the pinned message in the chat; 0 if none
//@reply_markup_message_id Identifier of the message from which reply markup needs to be used; 0 if there is no default custom reply markup in the chat
//@draft_message A draft of a message in the chat; may be null
//@client_data Contains client-specific data associated with the chat. (For example, the chat position or local chat notification settings can be stored here.) Persistent if a message database is used
chat id:int53 type:ChatType title:string photo:chatPhoto last_message:message order:int64 is_pinned:Bool is_marked_as_unread:Bool is_sponsored:Bool can_be_deleted_only_for_self:Bool can_be_deleted_for_all_users:Bool can_be_reported:Bool default_disable_notification:Bool unread_count:int32 last_read_inbox_message_id:int53 last_read_outbox_message_id:int53 unread_mention_count:int32 notification_settings:chatNotificationSettings pinned_message_id:int53 reply_markup_message_id:int53 draft_message:draftMessage client_data:string = Chat;

//@description Represents a list of chats @chat_ids List of chat identifiers
chats chat_ids:vector<int53> = Chats;


//@description Contains a chat invite link @invite_link Chat invite link
chatInviteLink invite_link:string = ChatInviteLink;

//@description Contains information about a chat invite link
//@chat_id Chat identifier of the invite link; 0 if the user is not a member of this chat
//@type Contains information about the type of the chat
//@title Title of the chat
//@photo Chat photo; may be null
//@member_count Number of members
//@member_user_ids User identifiers of some chat members that may be known to the current user
//@is_public True, if the chat is a public supergroup or channel with a username
chatInviteLinkInfo chat_id:int53 type:ChatType title:string photo:chatPhoto member_count:int32 member_user_ids:vector<int32> is_public:Bool = ChatInviteLinkInfo;


//@class KeyboardButtonType @description Describes a keyboard button type

//@description A simple button, with text that should be sent when the button is pressed
keyboardButtonTypeText = KeyboardButtonType;

//@description A button that sends the user's phone number when pressed; available only in private chats
keyboardButtonTypeRequestPhoneNumber = KeyboardButtonType;

//@description A button that sends the user's location when pressed; available only in private chats
keyboardButtonTypeRequestLocation = KeyboardButtonType;


//@description Represents a single button in a bot keyboard @text Text of the button @type Type of the button
keyboardButton text:string type:KeyboardButtonType = KeyboardButton;


//@class InlineKeyboardButtonType @description Describes the type of an inline keyboard button

//@description A button that opens a specified URL @url HTTP or tg:// URL to open
inlineKeyboardButtonTypeUrl url:string = InlineKeyboardButtonType;

//@description A button that sends a special callback query to a bot @data Data to be sent to the bot via a callback query
inlineKeyboardButtonTypeCallback data:bytes = InlineKeyboardButtonType;

//@description A button with a game that sends a special callback query to a bot. This button must be in the first column and row of the keyboard and can be attached only to a message with content of the type messageGame
inlineKeyboardButtonTypeCallbackGame = InlineKeyboardButtonType;

//@description A button that forces an inline query to the bot to be inserted in the input field @query Inline query to be sent to the bot @in_current_chat True, if the inline query should be sent from the current chat
inlineKeyboardButtonTypeSwitchInline query:string in_current_chat:Bool = InlineKeyboardButtonType;

//@description A button to buy something. This button must be in the first column and row of the keyboard and can be attached only to a message with content of the type messageInvoice
inlineKeyboardButtonTypeBuy = InlineKeyboardButtonType;


//@description Represents a single button in an inline keyboard @text Text of the button @type Type of the button
inlineKeyboardButton text:string type:InlineKeyboardButtonType = InlineKeyboardButton;


//@class ReplyMarkup @description Contains a description of a custom keyboard and actions that can be done with it to quickly reply to bots

//@description Instructs clients to remove the keyboard once this message has been received. This kind of keyboard can't be received in an incoming message; instead, UpdateChatReplyMarkup with message_id == 0 will be sent
//@is_personal True, if the keyboard is removed only for the mentioned users or the target user of a reply
replyMarkupRemoveKeyboard is_personal:Bool = ReplyMarkup;

//@description Instructs clients to force a reply to this message
//@is_personal True, if a forced reply must automatically be shown to the current user. For outgoing messages, specify true to show the forced reply only for the mentioned users and for the target user of a reply
replyMarkupForceReply is_personal:Bool = ReplyMarkup;

//@description Contains a custom keyboard layout to quickly reply to bots
//@rows A list of rows of bot keyboard buttons
//@resize_keyboard True, if the client needs to resize the keyboard vertically
//@one_time True, if the client needs to hide the keyboard after use
//@is_personal True, if the keyboard must automatically be shown to the current user. For outgoing messages, specify true to show the keyboard only for the mentioned users and for the target user of a reply
replyMarkupShowKeyboard rows:vector<vector<keyboardButton>> resize_keyboard:Bool one_time:Bool is_personal:Bool = ReplyMarkup;

//@description Contains an inline keyboard layout
//@rows A list of rows of inline keyboard buttons
replyMarkupInlineKeyboard rows:vector<vector<inlineKeyboardButton>> = ReplyMarkup;


//@class RichText @description Describes a text object inside an instant-view web page

//@description A plain text @text Text
richTextPlain text:string = RichText;

//@description A bold rich text @text Text
richTextBold text:RichText = RichText;

//@description An italicized rich text @text Text
richTextItalic text:RichText = RichText;

//@description An underlined rich text @text Text
richTextUnderline text:RichText = RichText;

//@description A strike-through rich text @text Text
richTextStrikethrough text:RichText = RichText;

//@description A fixed-width rich text @text Text
richTextFixed text:RichText = RichText;

//@description A rich text URL link @text Text @url URL
richTextUrl text:RichText url:string = RichText;

//@description A rich text email link @text Text @email_address Email address
richTextEmailAddress text:RichText email_address:string = RichText;

//@description A subscript rich text @text Text
richTextSubscript text:RichText = RichText;

//@description A superscript rich text @text Text
richTextSuperscript text:RichText = RichText;

//@description A marked rich text @text Text
richTextMarked text:RichText = RichText;

//@description A rich text phone number @text Text @phone_number Phone number
richTextPhoneNumber text:RichText phone_number:string = RichText;

//@description A small image inside the text @document The image represented as a document. The image can be in GIF, JPEG or PNG format
//@width Width of a bounding box in which the image should be shown, 0 if unknown
//@height Height of a bounding box in which the image should be shown, 0 if unknown
richTextIcon document:document width:int32 height:int32 = RichText;

//@description A rich text anchor @text Text @name Anchor name
richTextAnchor text:RichText name:string = RichText;

//@description A concatenation of rich texts @texts Texts
richTexts texts:vector<RichText> = RichText;


//@description Contains a caption of an instant view web page block, consisting of a text and a trailing credit @text Content of the caption @credit Block credit (like HTML tag <cite>)
pageBlockCaption text:RichText credit:RichText = PageBlockCaption;

//@description Describes an item of a list page block @label Item label @page_blocks Item blocks
pageBlockListItem label:string page_blocks:vector<PageBlock> = PageBlockListItem;

//@class PageBlockHorizontalAlignment @description Describes a horizontal alignment of a table cell content

//@description The content should be left-aligned
pageBlockHorizontalAlignmentLeft = PageBlockHorizontalAlignment;

//@description The content should be center-aligned
pageBlockHorizontalAlignmentCenter = PageBlockHorizontalAlignment;

//@description The content should be right-aligned
pageBlockHorizontalAlignmentRight = PageBlockHorizontalAlignment;

//@class PageBlockVerticalAlignment @description Describes a Vertical alignment of a table cell content

//@description The content should be top-aligned
pageBlockVerticalAlignmentTop = PageBlockVerticalAlignment;

//@description The content should be middle-aligned
pageBlockVerticalAlignmentMiddle = PageBlockVerticalAlignment;

//@description The content should be bottom-aligned
pageBlockVerticalAlignmentBottom = PageBlockVerticalAlignment;

//@description Represents a cell of a table @text Cell text @is_header True, if it is a header cell
//@colspan The number of columns the cell should span @rowspan The number of rows the cell should span
//@align Horizontal cell content alignment @valign Vertical cell content alignment
pageBlockTableCell text:RichText is_header:Bool colspan:int32 rowspan:int32 align:PageBlockHorizontalAlignment valign:PageBlockVerticalAlignment = PageBlockTableCell;

//@description Contains information about a related article @url Related article URL @title Article title; may be empty @param_description Article description; may be empty
//@photo Article photo; may be null @author Article author; may be empty @publish_date Point in time (Unix timestamp) when the article was published; 0 if unknown
pageBlockRelatedArticle url:string title:string description:string photo:photo author:string publish_date:int32 = PageBlockRelatedArticle;


//@class PageBlock @description Describes a block of an instant view web page

//@description The title of a page @title Title
pageBlockTitle title:RichText = PageBlock;

//@description The subtitle of a page @subtitle Subtitle
pageBlockSubtitle subtitle:RichText = PageBlock;

//@description The author and publishing date of a page @author Author @publish_date Point in time (Unix timestamp) when the article was published; 0 if unknown
pageBlockAuthorDate author:RichText publish_date:int32 = PageBlock;

//@description A header @header Header
pageBlockHeader header:RichText = PageBlock;

//@description A subheader @subheader Subheader
pageBlockSubheader subheader:RichText = PageBlock;

//@description A kicker @kicker Kicker
pageBlockKicker kicker:RichText = PageBlock;

//@description A text paragraph @text Paragraph text
pageBlockParagraph text:RichText = PageBlock;

//@description A preformatted text paragraph @text Paragraph text @language Programming language for which the text should be formatted
pageBlockPreformatted text:RichText language:string = PageBlock;

//@description The footer of a page @footer Footer
pageBlockFooter footer:RichText = PageBlock;

//@description An empty block separating a page
pageBlockDivider = PageBlock;

//@description An invisible anchor on a page, which can be used in a URL to open the page from the specified anchor @name Name of the anchor
pageBlockAnchor name:string = PageBlock;

//@description A list of data blocks @items The items of the list
pageBlockList items:vector<pageBlockListItem> = PageBlock;

//@description A block quote @text Quote text @credit Quote credit
pageBlockBlockQuote text:RichText credit:RichText = PageBlock;

//@description A pull quote @text Quote text @credit Quote credit
pageBlockPullQuote text:RichText credit:RichText = PageBlock;

//@description An animation @animation Animation file; may be null @caption Animation caption @need_autoplay True, if the animation should be played automatically
pageBlockAnimation animation:animation caption:pageBlockCaption need_autoplay:Bool = PageBlock;

//@description An audio file @audio Audio file; may be null @caption Audio file caption
pageBlockAudio audio:audio caption:pageBlockCaption = PageBlock;

//@description A photo @photo Photo file; may be null @caption Photo caption @url URL that needs to be opened when the photo is clicked
pageBlockPhoto photo:photo caption:pageBlockCaption url:string = PageBlock;

//@description A video @video Video file; may be null @caption Video caption @need_autoplay True, if the video should be played automatically @is_looped True, if the video should be looped
pageBlockVideo video:video caption:pageBlockCaption need_autoplay:Bool is_looped:Bool = PageBlock;

//@description A page cover @cover Cover
pageBlockCover cover:PageBlock = PageBlock;

//@description An embedded web page @url Web page URL, if available @html HTML-markup of the embedded page @poster_photo Poster photo, if available; may be null @width Block width, 0 if unknown @height Block height, 0 if unknown @caption Block caption @is_full_width True, if the block should be full width @allow_scrolling True, if scrolling should be allowed
pageBlockEmbedded url:string html:string poster_photo:photo width:int32 height:int32 caption:pageBlockCaption is_full_width:Bool allow_scrolling:Bool = PageBlock;

//@description An embedded post @url Web page URL @author Post author @author_photo Post author photo @date Point in time (Unix timestamp) when the post was created; 0 if unknown @page_blocks Post content @caption Post caption
pageBlockEmbeddedPost url:string author:string author_photo:photo date:int32 page_blocks:vector<PageBlock> caption:pageBlockCaption = PageBlock;

//@description A collage @page_blocks Collage item contents @caption Block caption
pageBlockCollage page_blocks:vector<PageBlock> caption:pageBlockCaption = PageBlock;

//@description A slideshow @page_blocks Slideshow item contents @caption Block caption
pageBlockSlideshow page_blocks:vector<PageBlock> caption:pageBlockCaption = PageBlock;

//@description A link to a chat @title Chat title @photo Chat photo; may be null @username Chat username, by which all other information about the chat should be resolved
pageBlockChatLink title:string photo:chatPhoto username:string = PageBlock;

//@description A table @caption Table caption @cells Table cells @is_bordered True, if the table is bordered @is_striped True, if the table is striped
pageBlockTable caption:RichText cells:vector<vector<pageBlockTableCell>> is_bordered:Bool is_striped:Bool = PageBlock;

//@description A collapsible block @header Always visible heading for the block @page_blocks Block contents @is_open True, if the block is open by default
pageBlockDetails header:RichText page_blocks:vector<PageBlock> is_open:Bool = PageBlock;

//@description Related articles @header Block header @articles List of related articles
pageBlockRelatedArticles header:RichText articles:vector<pageBlockRelatedArticle> = PageBlock;

//@description A map @location Location of the map center @zoom Map zoom level @width Map width @height Map height @caption Block caption
pageBlockMap location:location zoom:int32 width:int32 height:int32 caption:pageBlockCaption = PageBlock;


//@description Describes an instant view page for a web page @page_blocks Content of the web page
//@version Version of the instant view, currently can be 1 or 2
//@url Instant view URL; may be different from WebPage.url and must be used for the correct anchors handling
//@is_rtl True, if the instant view must be shown from right to left
//@is_full True, if the instant view contains the full page. A network request might be needed to get the full web page instant view
webPageInstantView page_blocks:vector<PageBlock> version:int32 url:string is_rtl:Bool is_full:Bool = WebPageInstantView;


//@description Describes a web page preview @url Original URL of the link @display_url URL to display
//@type Type of the web page. Can be: article, photo, audio, video, document, profile, app, or something else
//@site_name Short name of the site (e.g., Google Docs, App Store) @title Title of the content @param_description Description of the content
//@photo Image representing the content; may be null
//@embed_url URL to show in the embedded preview
//@embed_type MIME type of the embedded preview, (e.g., text/html or video/mp4)
//@embed_width Width of the embedded preview
//@embed_height Height of the embedded preview
//@duration Duration of the content, in seconds
//@author Author of the content
//@animation Preview of the content as an animation, if available; may be null
//@audio Preview of the content as an audio file, if available; may be null
//@document Preview of the content as a document, if available (currently only available for small PDF files and ZIP archives); may be null
//@sticker Preview of the content as a sticker for small WEBP files, if available; may be null
//@video Preview of the content as a video, if available; may be null
//@video_note Preview of the content as a video note, if available; may be null
//@voice_note Preview of the content as a voice note, if available; may be null
//@instant_view_version Version of instant view, available for the web page (currently can be 1 or 2), 0 if none
webPage url:string display_url:string type:string site_name:string title:string description:string photo:photo embed_url:string embed_type:string embed_width:int32 embed_height:int32 duration:int32 author:string animation:animation audio:audio document:document sticker:sticker video:video video_note:videoNote voice_note:voiceNote instant_view_version:int32 = WebPage;


//@description Describes an address @country_code A two-letter ISO 3166-1 alpha-2 country code @state State, if applicable @city City @street_line1 First line of the address @street_line2 Second line of the address @postal_code Address postal code
address country_code:string state:string city:string street_line1:string street_line2:string postal_code:string = Address;


//@description Portion of the price of a product (e.g., "delivery cost", "tax amount") @label Label for this portion of the product price @amount Currency amount in minimal quantity of the currency
labeledPricePart label:string amount:int53 = LabeledPricePart;

//@description Product invoice @currency ISO 4217 currency code @price_parts A list of objects used to calculate the total price of the product @is_test True, if the payment is a test payment
//@need_name True, if the user's name is needed for payment @need_phone_number True, if the user's phone number is needed for payment @need_email_address True, if the user's email address is needed for payment
//@need_shipping_address True, if the user's shipping address is needed for payment @send_phone_number_to_provider True, if the user's phone number will be sent to the provider
//@send_email_address_to_provider True, if the user's email address will be sent to the provider @is_flexible True, if the total price depends on the shipping method
invoice currency:string price_parts:vector<labeledPricePart> is_test:Bool need_name:Bool need_phone_number:Bool need_email_address:Bool need_shipping_address:Bool send_phone_number_to_provider:Bool send_email_address_to_provider:Bool is_flexible:Bool = Invoice;

//@description Order information @name Name of the user @phone_number Phone number of the user @email_address Email address of the user @shipping_address Shipping address for this order; may be null
orderInfo name:string phone_number:string email_address:string shipping_address:address = OrderInfo;

//@description One shipping option @id Shipping option identifier @title Option title @price_parts A list of objects used to calculate the total shipping costs
shippingOption id:string title:string price_parts:vector<labeledPricePart> = ShippingOption;

//@description Contains information about saved card credentials @id Unique identifier of the saved credentials @title Title of the saved credentials
savedCredentials id:string title:string = SavedCredentials;

//@class InputCredentials @description Contains information about the payment method chosen by the user

//@description Applies if a user chooses some previously saved payment credentials. To use their previously saved credentials, the user must have a valid temporary password @saved_credentials_id Identifier of the saved credentials
inputCredentialsSaved saved_credentials_id:string = InputCredentials;

//@description Applies if a user enters new credentials on a payment provider website @data Contains JSON-encoded data with a credential identifier from the payment provider @allow_save True, if the credential identifier can be saved on the server side
inputCredentialsNew data:string allow_save:Bool = InputCredentials;

//@description Applies if a user enters new credentials using Android Pay @data JSON-encoded data with the credential identifier
inputCredentialsAndroidPay data:string = InputCredentials;

//@description Applies if a user enters new credentials using Apple Pay @data JSON-encoded data with the credential identifier
inputCredentialsApplePay data:string = InputCredentials;

//@description Stripe payment provider @publishable_key Stripe API publishable key @need_country True, if the user country must be provided @need_postal_code True, if the user ZIP/postal code must be provided @need_cardholder_name True, if the cardholder name must be provided
paymentsProviderStripe publishable_key:string need_country:Bool need_postal_code:Bool need_cardholder_name:Bool = PaymentsProviderStripe;

//@description Contains information about an invoice payment form @invoice Full information of the invoice @url Payment form URL @payments_provider Contains information about the payment provider, if available, to support it natively without the need for opening the URL; may be null
//@saved_order_info Saved server-side order information; may be null @saved_credentials Contains information about saved card credentials; may be null @can_save_credentials True, if the user can choose to save credentials @need_password True, if the user will be able to save credentials protected by a password they set up
paymentForm invoice:invoice url:string payments_provider:paymentsProviderStripe saved_order_info:orderInfo saved_credentials:savedCredentials can_save_credentials:Bool need_password:Bool = PaymentForm;

//@description Contains a temporary identifier of validated order information, which is stored for one hour. Also contains the available shipping options @order_info_id Temporary identifier of the order information @shipping_options Available shipping options
validatedOrderInfo order_info_id:string shipping_options:vector<shippingOption> = ValidatedOrderInfo;

//@description Contains the result of a payment request @success True, if the payment request was successful; otherwise the verification_url will be not empty @verification_url URL for additional payment credentials verification
paymentResult success:Bool verification_url:string = PaymentResult;

//@description Contains information about a successful payment @date Point in time (Unix timestamp) when the payment was made @payments_provider_user_id User identifier of the payment provider bot @invoice Contains information about the invoice
//@order_info Contains order information; may be null @shipping_option Chosen shipping option; may be null @credentials_title Title of the saved credentials
paymentReceipt date:int32 payments_provider_user_id:int32 invoice:invoice order_info:orderInfo shipping_option:shippingOption credentials_title:string = PaymentReceipt;


//@description File with the date it was uploaded @file The file @date Point in time (Unix timestamp) when the file was uploaded
datedFile file:file date:int32 = DatedFile;


//@class PassportElementType @description Contains the type of a Telegram Passport element

//@description A Telegram Passport element containing the user's personal details
passportElementTypePersonalDetails = PassportElementType;

//@description A Telegram Passport element containing the user's passport
passportElementTypePassport = PassportElementType;

//@description A Telegram Passport element containing the user's driver license
passportElementTypeDriverLicense = PassportElementType;

//@description A Telegram Passport element containing the user's identity card
passportElementTypeIdentityCard = PassportElementType;

//@description A Telegram Passport element containing the user's internal passport
passportElementTypeInternalPassport = PassportElementType;

//@description A Telegram Passport element containing the user's address
passportElementTypeAddress = PassportElementType;

//@description A Telegram Passport element containing the user's utility bill
passportElementTypeUtilityBill = PassportElementType;

//@description A Telegram Passport element containing the user's bank statement
passportElementTypeBankStatement = PassportElementType;

//@description A Telegram Passport element containing the user's rental agreement
passportElementTypeRentalAgreement = PassportElementType;

//@description A Telegram Passport element containing the registration page of the user's passport
passportElementTypePassportRegistration = PassportElementType;

//@description A Telegram Passport element containing the user's temporary registration
passportElementTypeTemporaryRegistration = PassportElementType;

//@description A Telegram Passport element containing the user's phone number
passportElementTypePhoneNumber = PassportElementType;

//@description A Telegram Passport element containing the user's email address
passportElementTypeEmailAddress = PassportElementType;


//@description Represents a date according to the Gregorian calendar @day Day of the month, 1-31 @month Month, 1-12 @year Year, 1-9999
date day:int32 month:int32 year:int32 = Date;

//@description Contains the user's personal details
//@first_name First name of the user written in English; 1-255 characters @middle_name Middle name of the user written in English; 0-255 characters @last_name Last name of the user written in English; 1-255 characters
//@native_first_name Native first name of the user; 1-255 characters @native_middle_name Native middle name of the user; 0-255 characters @native_last_name Native last name of the user; 1-255 characters
//@birthdate Birthdate of the user @gender Gender of the user, "male" or "female" @country_code A two-letter ISO 3166-1 alpha-2 country code of the user's country @residence_country_code A two-letter ISO 3166-1 alpha-2 country code of the user's residence country
personalDetails first_name:string middle_name:string last_name:string native_first_name:string native_middle_name:string native_last_name:string birthdate:date gender:string country_code:string residence_country_code:string = PersonalDetails;

//@description An identity document @number Document number; 1-24 characters @expiry_date Document expiry date; may be null @front_side Front side of the document
//@reverse_side Reverse side of the document; only for driver license and identity card @selfie Selfie with the document; may be null @translation List of files containing a certified English translation of the document
identityDocument number:string expiry_date:date front_side:datedFile reverse_side:datedFile selfie:datedFile translation:vector<datedFile> = IdentityDocument;

//@description An identity document to be saved to Telegram Passport @number Document number; 1-24 characters @expiry_date Document expiry date, if available @front_side Front side of the document
//@reverse_side Reverse side of the document; only for driver license and identity card @selfie Selfie with the document, if available @translation List of files containing a certified English translation of the document
inputIdentityDocument number:string expiry_date:date front_side:InputFile reverse_side:InputFile selfie:InputFile translation:vector<InputFile> = InputIdentityDocument;

//@description A personal document, containing some information about a user @files List of files containing the pages of the document @translation List of files containing a certified English translation of the document
personalDocument files:vector<datedFile> translation:vector<datedFile> = PersonalDocument;

//@description A personal document to be saved to Telegram Passport @files List of files containing the pages of the document @translation List of files containing a certified English translation of the document
inputPersonalDocument files:vector<InputFile> translation:vector<InputFile> = InputPersonalDocument;


//@class PassportElement @description Contains information about a Telegram Passport element

//@description A Telegram Passport element containing the user's personal details @personal_details Personal details of the user
passportElementPersonalDetails personal_details:personalDetails = PassportElement;

//@description A Telegram Passport element containing the user's passport @passport Passport
passportElementPassport passport:identityDocument = PassportElement;

//@description A Telegram Passport element containing the user's driver license @driver_license Driver license
passportElementDriverLicense driver_license:identityDocument = PassportElement;

//@description A Telegram Passport element containing the user's identity card @identity_card Identity card
passportElementIdentityCard identity_card:identityDocument = PassportElement;

//@description A Telegram Passport element containing the user's internal passport @internal_passport Internal passport
passportElementInternalPassport internal_passport:identityDocument = PassportElement;

//@description A Telegram Passport element containing the user's address @address Address
passportElementAddress address:address = PassportElement;

//@description A Telegram Passport element containing the user's utility bill @utility_bill Utility bill
passportElementUtilityBill utility_bill:personalDocument = PassportElement;

//@description A Telegram Passport element containing the user's bank statement @bank_statement Bank statement
passportElementBankStatement bank_statement:personalDocument = PassportElement;

//@description A Telegram Passport element containing the user's rental agreement @rental_agreement Rental agreement
passportElementRentalAgreement rental_agreement:personalDocument = PassportElement;

//@description A Telegram Passport element containing the user's passport registration pages @passport_registration Passport registration pages
passportElementPassportRegistration passport_registration:personalDocument = PassportElement;

//@description A Telegram Passport element containing the user's temporary registration @temporary_registration Temporary registration
passportElementTemporaryRegistration temporary_registration:personalDocument = PassportElement;

//@description A Telegram Passport element containing the user's phone number @phone_number Phone number
passportElementPhoneNumber phone_number:string = PassportElement;

//@description A Telegram Passport element containing the user's email address @email_address Email address
passportElementEmailAddress email_address:string = PassportElement;


//@class InputPassportElement @description Contains information about a Telegram Passport element to be saved

//@description A Telegram Passport element to be saved containing the user's personal details @personal_details Personal details of the user
inputPassportElementPersonalDetails personal_details:personalDetails = InputPassportElement;

//@description A Telegram Passport element to be saved containing the user's passport @passport The passport to be saved
inputPassportElementPassport passport:inputIdentityDocument = InputPassportElement;

//@description A Telegram Passport element to be saved containing the user's driver license @driver_license The driver license to be saved
inputPassportElementDriverLicense driver_license:inputIdentityDocument = InputPassportElement;

//@description A Telegram Passport element to be saved containing the user's identity card @identity_card The identity card to be saved
inputPassportElementIdentityCard identity_card:inputIdentityDocument = InputPassportElement;

//@description A Telegram Passport element to be saved containing the user's internal passport @internal_passport The internal passport to be saved
inputPassportElementInternalPassport internal_passport:inputIdentityDocument = InputPassportElement;

//@description A Telegram Passport element to be saved containing the user's address @address The address to be saved
inputPassportElementAddress address:address = InputPassportElement;

//@description A Telegram Passport element to be saved containing the user's utility bill @utility_bill The utility bill to be saved
inputPassportElementUtilityBill utility_bill:inputPersonalDocument = InputPassportElement;

//@description A Telegram Passport element to be saved containing the user's bank statement @bank_statement The bank statement to be saved
inputPassportElementBankStatement bank_statement:inputPersonalDocument = InputPassportElement;

//@description A Telegram Passport element to be saved containing the user's rental agreement @rental_agreement The rental agreement to be saved
inputPassportElementRentalAgreement rental_agreement:inputPersonalDocument = InputPassportElement;

//@description A Telegram Passport element to be saved containing the user's passport registration @passport_registration The passport registration page to be saved
inputPassportElementPassportRegistration passport_registration:inputPersonalDocument = InputPassportElement;

//@description A Telegram Passport element to be saved containing the user's temporary registration @temporary_registration The temporary registration document to be saved
inputPassportElementTemporaryRegistration temporary_registration:inputPersonalDocument = InputPassportElement;

//@description A Telegram Passport element to be saved containing the user's phone number @phone_number The phone number to be saved
inputPassportElementPhoneNumber phone_number:string = InputPassportElement;

//@description A Telegram Passport element to be saved containing the user's email address @email_address The email address to be saved
inputPassportElementEmailAddress email_address:string = InputPassportElement;


//@description Contains information about saved Telegram Passport elements @elements Telegram Passport elements
passportElements elements:vector<PassportElement> = PassportElements;


//@class PassportElementErrorSource @description Contains the description of an error in a Telegram Passport element

//@description The element contains an error in an unspecified place. The error will be considered resolved when new data is added
passportElementErrorSourceUnspecified = PassportElementErrorSource;

//@description One of the data fields contains an error. The error will be considered resolved when the value of the field changes @field_name Field name
passportElementErrorSourceDataField field_name:string = PassportElementErrorSource;

//@description The front side of the document contains an error. The error will be considered resolved when the file with the front side changes
passportElementErrorSourceFrontSide = PassportElementErrorSource;

//@description The reverse side of the document contains an error. The error will be considered resolved when the file with the reverse side changes
passportElementErrorSourceReverseSide = PassportElementErrorSource;

//@description The selfie with the document contains an error. The error will be considered resolved when the file with the selfie changes
passportElementErrorSourceSelfie = PassportElementErrorSource;

//@description One of files with the translation of the document contains an error. The error will be considered resolved when the file changes @file_index Index of a file with the error
passportElementErrorSourceTranslationFile file_index:int32 = PassportElementErrorSource;

//@description The translation of the document contains an error. The error will be considered resolved when the list of translation files changes
passportElementErrorSourceTranslationFiles = PassportElementErrorSource;

//@description The file contains an error. The error will be considered resolved when the file changes @file_index Index of a file with the error
passportElementErrorSourceFile file_index:int32 = PassportElementErrorSource;

//@description The list of attached files contains an error. The error will be considered resolved when the list of files changes
passportElementErrorSourceFiles = PassportElementErrorSource;


//@description Contains the description of an error in a Telegram Passport element @type Type of the Telegram Passport element which has the error @message Error message @source Error source
passportElementError type:PassportElementType message:string source:PassportElementErrorSource = PassportElementError;


//@description Contains information about a Telegram Passport element that was requested by a service @type Type of the element @is_selfie_required True, if a selfie is required with the identity document
//@is_translation_required True, if a certified English translation is required with the document @is_native_name_required True, if personal details must include the user's name in the language of their country of residence
passportSuitableElement type:PassportElementType is_selfie_required:Bool is_translation_required:Bool is_native_name_required:Bool = PassportSuitableElement;

//@description Contains a description of the required Telegram Passport element that was requested by a service @suitable_elements List of Telegram Passport elements any of which is enough to provide
passportRequiredElement suitable_elements:vector<passportSuitableElement> = PassportRequiredElement;

//@description Contains information about a Telegram Passport authorization form that was requested @id Unique identifier of the authorization form
//@required_elements Information about the Telegram Passport elements that need to be provided to complete the form
//@privacy_policy_url URL for the privacy policy of the service; may be empty
passportAuthorizationForm id:int32 required_elements:vector<passportRequiredElement> privacy_policy_url:string = PassportAuthorizationForm;

//@description Contains information about a Telegram Passport elements and corresponding errors @elements Telegram Passport elements @errors Errors in the elements that are already available
passportElementsWithErrors elements:vector<PassportElement> errors:vector<passportElementError> = PassportElementsWithErrors;


//@description Contains encrypted Telegram Passport data credentials @data The encrypted credentials @hash The decrypted data hash @secret Secret for data decryption, encrypted with the service's public key
encryptedCredentials data:bytes hash:bytes secret:bytes = EncryptedCredentials;


//@description Contains information about an encrypted Telegram Passport element; for bots only @type Type of Telegram Passport element @data Encrypted JSON-encoded data about the user @front_side The front side of an identity document @reverse_side The reverse side of an identity document; may be null @selfie Selfie with the document; may be null @translation List of files containing a certified English translation of the document @files List of attached files @value Unencrypted data, phone number or email address @hash Hash of the entire element
encryptedPassportElement type:PassportElementType data:bytes front_side:datedFile reverse_side:datedFile selfie:datedFile translation:vector<datedFile> files:vector<datedFile> value:string hash:string = EncryptedPassportElement;


//@class InputPassportElementErrorSource @description Contains the description of an error in a Telegram Passport element; for bots only

//@description The element contains an error in an unspecified place. The error will be considered resolved when new data is added @element_hash Current hash of the entire element
inputPassportElementErrorSourceUnspecified element_hash:bytes = InputPassportElementErrorSource;

//@description A data field contains an error. The error is considered resolved when the field's value changes @field_name Field name @data_hash Current data hash
inputPassportElementErrorSourceDataField field_name:string data_hash:bytes = InputPassportElementErrorSource;

//@description The front side of the document contains an error. The error is considered resolved when the file with the front side of the document changes @file_hash Current hash of the file containing the front side
inputPassportElementErrorSourceFrontSide file_hash:bytes = InputPassportElementErrorSource;

//@description The reverse side of the document contains an error. The error is considered resolved when the file with the reverse side of the document changes @file_hash Current hash of the file containing the reverse side
inputPassportElementErrorSourceReverseSide file_hash:bytes = InputPassportElementErrorSource;

//@description The selfie contains an error. The error is considered resolved when the file with the selfie changes @file_hash Current hash of the file containing the selfie
inputPassportElementErrorSourceSelfie file_hash:bytes = InputPassportElementErrorSource;

//@description One of the files containing the translation of the document contains an error. The error is considered resolved when the file with the translation changes @file_hash Current hash of the file containing the translation
inputPassportElementErrorSourceTranslationFile file_hash:bytes = InputPassportElementErrorSource;

//@description The translation of the document contains an error. The error is considered resolved when the list of files changes @file_hashes Current hashes of all files with the translation
inputPassportElementErrorSourceTranslationFiles file_hashes:vector<bytes> = InputPassportElementErrorSource;

//@description The file contains an error. The error is considered resolved when the file changes @file_hash Current hash of the file which has the error
inputPassportElementErrorSourceFile file_hash:bytes = InputPassportElementErrorSource;

//@description The list of attached files contains an error. The error is considered resolved when the file list changes @file_hashes Current hashes of all attached files
inputPassportElementErrorSourceFiles file_hashes:vector<bytes> = InputPassportElementErrorSource;


//@description Contains the description of an error in a Telegram Passport element; for bots only @type Type of Telegram Passport element that has the error @message Error message @source Error source
inputPassportElementError type:PassportElementType message:string source:InputPassportElementErrorSource = InputPassportElementError;


//@class MessageContent @description Contains the content of a message

//@description A text message @text Text of the message @web_page A preview of the web page that's mentioned in the text; may be null
messageText text:formattedText web_page:webPage = MessageContent;

//@description An animation message (GIF-style). @animation Message content @caption Animation caption @is_secret True, if the animation thumbnail must be blurred and the animation must be shown only while tapped
messageAnimation animation:animation caption:formattedText is_secret:Bool = MessageContent;

//@description An audio message @audio Message content @caption Audio caption
messageAudio audio:audio caption:formattedText = MessageContent;

//@description A document message (general file) @document Message content @caption Document caption
messageDocument document:document caption:formattedText = MessageContent;

//@description A photo message @photo Message content @caption Photo caption @is_secret True, if the photo must be blurred and must be shown only while tapped
messagePhoto photo:photo caption:formattedText is_secret:Bool = MessageContent;

//@description An expired photo message (self-destructed after TTL has elapsed)
messageExpiredPhoto = MessageContent;

//@description A sticker message @sticker Message content
messageSticker sticker:sticker = MessageContent;

//@description A video message @video Message content @caption Video caption @is_secret True, if the video thumbnail must be blurred and the video must be shown only while tapped
messageVideo video:video caption:formattedText is_secret:Bool = MessageContent;

//@description An expired video message (self-destructed after TTL has elapsed)
messageExpiredVideo = MessageContent;

//@description A video note message @video_note Message content @is_viewed True, if at least one of the recipients has viewed the video note @is_secret True, if the video note thumbnail must be blurred and the video note must be shown only while tapped
messageVideoNote video_note:videoNote is_viewed:Bool is_secret:Bool = MessageContent;

//@description A voice note message @voice_note Message content @caption Voice note caption @is_listened True, if at least one of the recipients has listened to the voice note
messageVoiceNote voice_note:voiceNote caption:formattedText is_listened:Bool = MessageContent;

//@description A message with a location @location Message content @live_period Time relative to the message sent date until which the location can be updated, in seconds
//@expires_in Left time for which the location can be updated, in seconds. updateMessageContent is not sent when this field changes
messageLocation location:location live_period:int32 expires_in:int32 = MessageContent;

//@description A message with information about a venue @venue Message content
messageVenue venue:venue = MessageContent;

//@description A message with a user contact @contact Message content
messageContact contact:contact = MessageContent;

//@description A message with a game @game Game
messageGame game:game = MessageContent;

//@description A message with a poll @poll Poll
messagePoll poll:poll = MessageContent;

//@description A message with an invoice from a bot @title Product title @param_description Product description @photo Product photo; may be null @currency Currency for the product price @total_amount Product total price in the minimal quantity of the currency
//@start_parameter Unique invoice bot start_parameter. To share an invoice use the URL https://t.me/{bot_username}?start={start_parameter} @is_test True, if the invoice is a test invoice
//@need_shipping_address True, if the shipping address should be specified @receipt_message_id The identifier of the message with the receipt, after the product has been purchased
messageInvoice title:string description:string photo:photo currency:string total_amount:int53 start_parameter:string is_test:Bool need_shipping_address:Bool receipt_message_id:int53 = MessageContent;

//@description A message with information about an ended call @discard_reason Reason why the call was discarded @duration Call duration, in seconds
messageCall discard_reason:CallDiscardReason duration:int32 = MessageContent;

//@description A newly created basic group @title Title of the basic group @member_user_ids User identifiers of members in the basic group
messageBasicGroupChatCreate title:string member_user_ids:vector<int32> = MessageContent;

//@description A newly created supergroup or channel @title Title of the supergroup or channel
messageSupergroupChatCreate title:string = MessageContent;

//@description An updated chat title @title New chat title
messageChatChangeTitle title:string = MessageContent;

//@description An updated chat photo @photo New chat photo
messageChatChangePhoto photo:photo = MessageContent;

//@description A deleted chat photo
messageChatDeletePhoto = MessageContent;

//@description New chat members were added @member_user_ids User identifiers of the new members
messageChatAddMembers member_user_ids:vector<int32> = MessageContent;

//@description A new member joined the chat by invite link
messageChatJoinByLink = MessageContent;

//@description A chat member was deleted @user_id User identifier of the deleted chat member
messageChatDeleteMember user_id:int32 = MessageContent;

//@description A basic group was upgraded to a supergroup and was deactivated as the result @supergroup_id Identifier of the supergroup to which the basic group was upgraded
messageChatUpgradeTo supergroup_id:int32 = MessageContent;

//@description A supergroup has been created from a basic group @title Title of the newly created supergroup @basic_group_id The identifier of the original basic group
messageChatUpgradeFrom title:string basic_group_id:int32 = MessageContent;

//@description A message has been pinned @message_id Identifier of the pinned message, can be an identifier of a deleted message or 0
messagePinMessage message_id:int53 = MessageContent;

//@description A screenshot of a message in the chat has been taken
messageScreenshotTaken = MessageContent;

//@description The TTL (Time To Live) setting messages in a secret chat has been changed @ttl New TTL
messageChatSetTtl ttl:int32 = MessageContent;

//@description A non-standard action has happened in the chat @text Message text to be shown in the chat
messageCustomServiceAction text:string = MessageContent;

//@description A new high score was achieved in a game @game_message_id Identifier of the message with the game, can be an identifier of a deleted message @game_id Identifier of the game; may be different from the games presented in the message with the game @score New score
messageGameScore game_message_id:int53 game_id:int64 score:int32 = MessageContent;

//@description A payment has been completed @invoice_message_id Identifier of the message with the corresponding invoice; can be an identifier of a deleted message @currency Currency for the price of the product @total_amount Total price for the product, in the minimal quantity of the currency
messagePaymentSuccessful invoice_message_id:int53 currency:string total_amount:int53 = MessageContent;

//@description A payment has been completed; for bots only @invoice_message_id Identifier of the message with the corresponding invoice; can be an identifier of a deleted message @currency Currency for price of the product
//@total_amount Total price for the product, in the minimal quantity of the currency @invoice_payload Invoice payload @shipping_option_id Identifier of the shipping option chosen by the user; may be empty if not applicable @order_info Information about the order; may be null
//@telegram_payment_charge_id Telegram payment identifier @provider_payment_charge_id Provider payment identifier
messagePaymentSuccessfulBot invoice_message_id:int53 currency:string total_amount:int53 invoice_payload:bytes shipping_option_id:string order_info:orderInfo telegram_payment_charge_id:string provider_payment_charge_id:string = MessageContent;

//@description A contact has registered with Telegram
messageContactRegistered = MessageContent;

//@description The current user has connected a website by logging in using Telegram Login Widget on it @domain_name Domain name of the connected website
messageWebsiteConnected domain_name:string = MessageContent;

//@description Telegram Passport data has been sent @types List of Telegram Passport element types sent
messagePassportDataSent types:vector<PassportElementType> = MessageContent;

//@description Telegram Passport data has been received; for bots only @elements List of received Telegram Passport elements @credentials Encrypted data credentials
messagePassportDataReceived elements:vector<encryptedPassportElement> credentials:encryptedCredentials = MessageContent;

//@description Message content that is not supported by the client
messageUnsupported = MessageContent;


//@class TextEntityType @description Represents a part of the text which must be formatted differently

//@description A mention of a user by their username
textEntityTypeMention = TextEntityType;

//@description A hashtag text, beginning with "#"
textEntityTypeHashtag = TextEntityType;

//@description A cashtag text, beginning with "$" and consisting of capital english letters (i.e. "$USD")
textEntityTypeCashtag = TextEntityType;

//@description A bot command, beginning with "/". This shouldn't be highlighted if there are no bots in the chat
textEntityTypeBotCommand = TextEntityType;

//@description An HTTP URL
textEntityTypeUrl = TextEntityType;

//@description An email address
textEntityTypeEmailAddress = TextEntityType;

//@description A bold text
textEntityTypeBold = TextEntityType;

//@description An italic text
textEntityTypeItalic = TextEntityType;

//@description Text that must be formatted as if inside a code HTML tag
textEntityTypeCode = TextEntityType;

//@description Text that must be formatted as if inside a pre HTML tag
textEntityTypePre = TextEntityType;

//@description Text that must be formatted as if inside pre, and code HTML tags @language Programming language of the code; as defined by the sender
textEntityTypePreCode language:string = TextEntityType;

//@description A text description shown instead of a raw URL @url HTTP or tg:// URL to be opened when the link is clicked
textEntityTypeTextUrl url:string = TextEntityType;

//@description A text shows instead of a raw mention of the user (e.g., when the user has no username) @user_id Identifier of the mentioned user
textEntityTypeMentionName user_id:int32 = TextEntityType;

//@description A phone number
textEntityTypePhoneNumber = TextEntityType;


//@description A thumbnail to be sent along with a file; should be in JPEG or WEBP format for stickers, and less than 200 kB in size @thumbnail Thumbnail file to send. Sending thumbnails by file_id is currently not supported
//@width Thumbnail width, usually shouldn't exceed 320. Use 0 if unknown @height Thumbnail height, usually shouldn't exceed 320. Use 0 if unknown
inputThumbnail thumbnail:InputFile width:int32 height:int32 = InputThumbnail;


//@class InputMessageContent @description The content of a message to send

//@description A text message @text Formatted text to be sent; 1-GetOption("message_text_length_max") characters. Only Bold, Italic, Code, Pre, PreCode and TextUrl entities are allowed to be specified manually
//@disable_web_page_preview True, if rich web page previews for URLs in the message text should be disabled @clear_draft True, if a chat message draft should be deleted
inputMessageText text:formattedText disable_web_page_preview:Bool clear_draft:Bool = InputMessageContent;

//@description An animation message (GIF-style). @animation Animation file to be sent @thumbnail Animation thumbnail, if available @duration Duration of the animation, in seconds @width Width of the animation; may be replaced by the server @height Height of the animation; may be replaced by the server @caption Animation caption; 0-GetOption("message_caption_length_max") characters
inputMessageAnimation animation:InputFile thumbnail:inputThumbnail duration:int32 width:int32 height:int32 caption:formattedText = InputMessageContent;

//@description An audio message @audio Audio file to be sent @album_cover_thumbnail Thumbnail of the cover for the album, if available @duration Duration of the audio, in seconds; may be replaced by the server @title Title of the audio; 0-64 characters; may be replaced by the server
//@performer Performer of the audio; 0-64 characters, may be replaced by the server @caption Audio caption; 0-GetOption("message_caption_length_max") characters
inputMessageAudio audio:InputFile album_cover_thumbnail:inputThumbnail duration:int32 title:string performer:string caption:formattedText = InputMessageContent;

//@description A document message (general file) @document Document to be sent @thumbnail Document thumbnail, if available @caption Document caption; 0-GetOption("message_caption_length_max") characters
inputMessageDocument document:InputFile thumbnail:inputThumbnail caption:formattedText = InputMessageContent;

//@description A photo message @photo Photo to send @thumbnail Photo thumbnail to be sent, this is sent to the other party in secret chats only @added_sticker_file_ids File identifiers of the stickers added to the photo, if applicable @width Photo width @height Photo height @caption Photo caption; 0-GetOption("message_caption_length_max") characters
//@ttl Photo TTL (Time To Live), in seconds (0-60). A non-zero TTL can be specified only in private chats
inputMessagePhoto photo:InputFile thumbnail:inputThumbnail added_sticker_file_ids:vector<int32> width:int32 height:int32 caption:formattedText ttl:int32 = InputMessageContent;

//@description A sticker message @sticker Sticker to be sent @thumbnail Sticker thumbnail, if available @width Sticker width @height Sticker height
inputMessageSticker sticker:InputFile thumbnail:inputThumbnail width:int32 height:int32 = InputMessageContent;

//@description A video message @video Video to be sent @thumbnail Video thumbnail, if available @added_sticker_file_ids File identifiers of the stickers added to the video, if applicable
//@duration Duration of the video, in seconds @width Video width @height Video height @supports_streaming True, if the video should be tried to be streamed
//@caption Video caption; 0-GetOption("message_caption_length_max") characters @ttl Video TTL (Time To Live), in seconds (0-60). A non-zero TTL can be specified only in private chats
inputMessageVideo video:InputFile thumbnail:inputThumbnail added_sticker_file_ids:vector<int32> duration:int32 width:int32 height:int32 supports_streaming:Bool caption:formattedText ttl:int32 = InputMessageContent;

//@description A video note message @video_note Video note to be sent @thumbnail Video thumbnail, if available @duration Duration of the video, in seconds @length Video width and height; must be positive and not greater than 640
inputMessageVideoNote video_note:InputFile thumbnail:inputThumbnail duration:int32 length:int32 = InputMessageContent;

//@description A voice note message @voice_note Voice note to be sent @duration Duration of the voice note, in seconds @waveform Waveform representation of the voice note, in 5-bit format @caption Voice note caption; 0-GetOption("message_caption_length_max") characters
inputMessageVoiceNote voice_note:InputFile duration:int32 waveform:bytes caption:formattedText = InputMessageContent;

//@description A message with a location @location Location to be sent @live_period Period for which the location can be updated, in seconds; should bebetween 60 and 86400 for a live location and 0 otherwise
inputMessageLocation location:location live_period:int32 = InputMessageContent;

//@description A message with information about a venue @venue Venue to send
inputMessageVenue venue:venue = InputMessageContent;

//@description A message containing a user contact @contact Contact to send
inputMessageContact contact:contact = InputMessageContent;

//@description A message with a game; not supported for channels or secret chats @bot_user_id User identifier of the bot that owns the game @game_short_name Short name of the game
inputMessageGame bot_user_id:int32 game_short_name:string = InputMessageContent;

//@description A message with an invoice; can be used only by bots and only in private chats @invoice Invoice @title Product title; 1-32 characters @param_description Product description; 0-255 characters @photo_url Product photo URL; optional @photo_size Product photo size @photo_width Product photo width @photo_height Product photo height
//@payload The invoice payload @provider_token Payment provider token @provider_data JSON-encoded data about the invoice, which will be shared with the payment provider @start_parameter Unique invoice bot start_parameter for the generation of this invoice
inputMessageInvoice invoice:invoice title:string description:string photo_url:string photo_size:int32 photo_width:int32 photo_height:int32 payload:bytes provider_token:string provider_data:string start_parameter:string = InputMessageContent;

//@description A message with a poll. Polls can't be sent to private or secret chats @question Poll question, 1-255 characters @options List of poll answer options, 2-10 strings 1-100 characters each
inputMessagePoll question:string options:vector<string> = InputMessageContent;

//@description A forwarded message @from_chat_id Identifier for the chat this forwarded message came from @message_id Identifier of the message to forward @in_game_share True, if a game message should be shared within a launched game; applies only to game messages
inputMessageForwarded from_chat_id:int53 message_id:int53 in_game_share:Bool = InputMessageContent;


//@class SearchMessagesFilter @description Represents a filter for message search results

//@description Returns all found messages, no filter is applied
searchMessagesFilterEmpty = SearchMessagesFilter;

//@description Returns only animation messages
searchMessagesFilterAnimation = SearchMessagesFilter;

//@description Returns only audio messages
searchMessagesFilterAudio = SearchMessagesFilter;

//@description Returns only document messages
searchMessagesFilterDocument = SearchMessagesFilter;

//@description Returns only photo messages
searchMessagesFilterPhoto = SearchMessagesFilter;

//@description Returns only video messages
searchMessagesFilterVideo = SearchMessagesFilter;

//@description Returns only voice note messages
searchMessagesFilterVoiceNote = SearchMessagesFilter;

//@description Returns only photo and video messages
searchMessagesFilterPhotoAndVideo = SearchMessagesFilter;

//@description Returns only messages containing URLs
searchMessagesFilterUrl = SearchMessagesFilter;

//@description Returns only messages containing chat photos
searchMessagesFilterChatPhoto = SearchMessagesFilter;

//@description Returns only call messages
searchMessagesFilterCall = SearchMessagesFilter;

//@description Returns only incoming call messages with missed/declined discard reasons
searchMessagesFilterMissedCall = SearchMessagesFilter;

//@description Returns only video note messages
searchMessagesFilterVideoNote = SearchMessagesFilter;

//@description Returns only voice and video note messages
searchMessagesFilterVoiceAndVideoNote = SearchMessagesFilter;

//@description Returns only messages with mentions of the current user, or messages that are replies to their messages
searchMessagesFilterMention = SearchMessagesFilter;

//@description Returns only messages with unread mentions of the current user, or messages that are replies to their messages. When using this filter the results can't be additionally filtered by a query or by the sending user
searchMessagesFilterUnreadMention = SearchMessagesFilter;


//@class ChatAction @description Describes the different types of activity in a chat

//@description The user is typing a message
chatActionTyping = ChatAction;
//@description The user is recording a video
chatActionRecordingVideo = ChatAction;
//@description The user is uploading a video @progress Upload progress, as a percentage
chatActionUploadingVideo progress:int32 = ChatAction;
//@description The user is recording a voice note
chatActionRecordingVoiceNote = ChatAction;
//@description The user is uploading a voice note @progress Upload progress, as a percentage
chatActionUploadingVoiceNote progress:int32 = ChatAction;
//@description The user is uploading a photo @progress Upload progress, as a percentage
chatActionUploadingPhoto progress:int32 = ChatAction;
//@description The user is uploading a document @progress Upload progress, as a percentage
chatActionUploadingDocument progress:int32 = ChatAction;
//@description The user is picking a location or venue to send
chatActionChoosingLocation = ChatAction;
//@description The user is picking a contact to send
chatActionChoosingContact = ChatAction;
//@description The user has started to play a game
chatActionStartPlayingGame = ChatAction;
//@description The user is recording a video note
chatActionRecordingVideoNote = ChatAction;
//@description The user is uploading a video note @progress Upload progress, as a percentage
chatActionUploadingVideoNote progress:int32 = ChatAction;
//@description The user has cancelled the previous action
chatActionCancel = ChatAction;


//@class UserStatus @description Describes the last time the user was online

//@description The user status was never changed
userStatusEmpty = UserStatus;

//@description The user is online @expires Point in time (Unix timestamp) when the user's online status will expire
userStatusOnline expires:int32 = UserStatus;

//@description The user is offline @was_online Point in time (Unix timestamp) when the user was last online
userStatusOffline was_online:int32 = UserStatus;

//@description The user was online recently
userStatusRecently = UserStatus;

//@description The user is offline, but was online last week
userStatusLastWeek = UserStatus;

//@description The user is offline, but was online last month
userStatusLastMonth = UserStatus;


//@description Represents a list of stickers @stickers List of stickers
stickers stickers:vector<sticker> = Stickers;

//@description Represents a list of all emoji corresponding to a sticker in a sticker set. The list is only for informational purposes, because a sticker is always sent with a fixed emoji from the corresponding Sticker object @emojis List of emojis
stickerEmojis emojis:vector<string> = StickerEmojis;

//@description Represents a sticker set @id Identifier of the sticker set @title Title of the sticker set @name Name of the sticker set @is_installed True, if the sticker set has been installed by the current user
//@is_archived True, if the sticker set has been archived. A sticker set can't be installed and archived simultaneously @is_official True, if the sticker set is official @is_masks True, if the stickers in the set are masks
//@is_viewed True for already viewed trending sticker sets @stickers List of stickers in this set @emojis A list of emoji corresponding to the stickers in the same order
stickerSet id:int64 title:string name:string is_installed:Bool is_archived:Bool is_official:Bool is_masks:Bool is_viewed:Bool stickers:vector<sticker> emojis:vector<stickerEmojis> = StickerSet;

//@description Represents short information about a sticker set @id Identifier of the sticker set @title Title of the sticker set @name Name of the sticker set @is_installed True, if the sticker set has been installed by current user
//@is_archived True, if the sticker set has been archived. A sticker set can't be installed and archived simultaneously @is_official True, if the sticker set is official @is_masks True, if the stickers in the set are masks
//@is_viewed True for already viewed trending sticker sets @size Total number of stickers in the set @covers Contains up to the first 5 stickers from the set, depending on the context. If the client needs more stickers the full set should be requested
stickerSetInfo id:int64 title:string name:string is_installed:Bool is_archived:Bool is_official:Bool is_masks:Bool is_viewed:Bool size:int32 covers:vector<sticker> = StickerSetInfo;

//@description Represents a list of sticker sets @total_count Approximate total number of sticker sets found @sets List of sticker sets
stickerSets total_count:int32 sets:vector<stickerSetInfo> = StickerSets;


//@class CallDiscardReason @description Describes the reason why a call was discarded

//@description The call wasn't discarded, or the reason is unknown
callDiscardReasonEmpty = CallDiscardReason;

//@description The call was ended before the conversation started. It was cancelled by the caller or missed by the other party
callDiscardReasonMissed = CallDiscardReason;

//@description The call was ended before the conversation started. It was declined by the other party
callDiscardReasonDeclined = CallDiscardReason;

//@description The call was ended during the conversation because the users were disconnected
callDiscardReasonDisconnected = CallDiscardReason;

//@description The call was ended because one of the parties hung up
callDiscardReasonHungUp = CallDiscardReason;


//@description Specifies the supported call protocols @udp_p2p True, if UDP peer-to-peer connections are supported @udp_reflector True, if connection through UDP reflectors is supported @min_layer Minimum supported API layer; use 65 @max_layer Maximum supported API layer; use 65
callProtocol udp_p2p:Bool udp_reflector:Bool min_layer:int32 max_layer:int32 = CallProtocol;

//@description Describes the address of UDP reflectors @id Reflector identifier @ip IPv4 reflector address @ipv6 IPv6 reflector address @port Reflector port number @peer_tag Connection peer tag
callConnection id:int64 ip:string ipv6:string port:int32 peer_tag:bytes = CallConnection;


//@description Contains the call identifier @id Call identifier
callId id:int32 = CallId;


//@class CallState @description Describes the current call state

//@description The call is pending, waiting to be accepted by a user @is_created True, if the call has already been created by the server @is_received True, if the call has already been received by the other party
callStatePending is_created:Bool is_received:Bool = CallState;

//@description The call has been answered and encryption keys are being exchanged
callStateExchangingKeys = CallState;

//@description The call is ready to use @protocol Call protocols supported by the peer @connections Available UDP reflectors @config A JSON-encoded call config @encryption_key Call encryption key @emojis Encryption key emojis fingerprint @allow_p2p True, if peer-to-peer connection is allowed by users privacy settings
callStateReady protocol:callProtocol connections:vector<callConnection> config:string encryption_key:bytes emojis:vector<string> allow_p2p:Bool = CallState;

//@description The call is hanging up after discardCall has been called
callStateHangingUp = CallState;

//@description The call has ended successfully @reason The reason, why the call has ended @need_rating True, if the call rating should be sent to the server @need_debug_information True, if the call debug information should be sent to the server
callStateDiscarded reason:CallDiscardReason need_rating:Bool need_debug_information:Bool = CallState;

//@description The call has ended with an error @error Error. An error with the code 4005000 will be returned if an outgoing call is missed because of an expired timeout
callStateError error:error = CallState;


//@description Describes a call @id Call identifier, not persistent @user_id Peer user identifier @is_outgoing True, if the call is outgoing @state Call state
call id:int32 user_id:int32 is_outgoing:Bool state:CallState = Call;


//@description Represents a list of animations @animations List of animations
animations animations:vector<animation> = Animations;


//@description Represents the result of an ImportContacts request @user_ids User identifiers of the imported contacts in the same order as they were specified in the request; 0 if the contact is not yet a registered user
//@importer_count The number of users that imported the corresponding contact; 0 for already registered users or if unavailable
importedContacts user_ids:vector<int32> importer_count:vector<int32> = ImportedContacts;


//@description Contains an HTTP URL @url The URL
httpUrl url:string = HttpUrl;


//@class InputInlineQueryResult @description Represents a single result of an inline query; for bots only

//@description Represents a link to an animated GIF @id Unique identifier of the query result @title Title of the query result @thumbnail_url URL of the static result thumbnail (JPEG or GIF), if it exists
//@gif_url The URL of the GIF-file (file size must not exceed 1MB) @gif_duration Duration of the GIF, in seconds @gif_width Width of the GIF @gif_height Height of the GIF
//@reply_markup The message reply markup. Must be of type replyMarkupInlineKeyboard or null
//@input_message_content The content of the message to be sent. Must be one of the following types: InputMessageText, InputMessageAnimation, InputMessageLocation, InputMessageVenue or InputMessageContact
inputInlineQueryResultAnimatedGif id:string title:string thumbnail_url:string gif_url:string gif_duration:int32 gif_width:int32 gif_height:int32 reply_markup:ReplyMarkup input_message_content:InputMessageContent = InputInlineQueryResult;

//@description Represents a link to an animated (i.e. without sound) H.264/MPEG-4 AVC video @id Unique identifier of the query result @title Title of the result @thumbnail_url URL of the static result thumbnail (JPEG or GIF), if it exists
//@mpeg4_url The URL of the MPEG4-file (file size must not exceed 1MB) @mpeg4_duration Duration of the video, in seconds @mpeg4_width Width of the video @mpeg4_height Height of the video
//@reply_markup The message reply markup. Must be of type replyMarkupInlineKeyboard or null
//@input_message_content The content of the message to be sent. Must be one of the following types: InputMessageText, InputMessageAnimation, InputMessageLocation, InputMessageVenue or InputMessageContact
inputInlineQueryResultAnimatedMpeg4 id:string title:string thumbnail_url:string mpeg4_url:string mpeg4_duration:int32 mpeg4_width:int32 mpeg4_height:int32 reply_markup:ReplyMarkup input_message_content:InputMessageContent = InputInlineQueryResult;

//@description Represents a link to an article or web page @id Unique identifier of the query result @url URL of the result, if it exists @hide_url True, if the URL must be not shown @title Title of the result
//@param_description A short description of the result @thumbnail_url URL of the result thumbnail, if it exists @thumbnail_width Thumbnail width, if known @thumbnail_height Thumbnail height, if known
//@reply_markup The message reply markup. Must be of type replyMarkupInlineKeyboard or null
//@input_message_content The content of the message to be sent. Must be one of the following types: InputMessageText, InputMessageLocation, InputMessageVenue or InputMessageContact
inputInlineQueryResultArticle id:string url:string hide_url:Bool title:string description:string thumbnail_url:string thumbnail_width:int32 thumbnail_height:int32 reply_markup:ReplyMarkup input_message_content:InputMessageContent = InputInlineQueryResult;

//@description Represents a link to an MP3 audio file @id Unique identifier of the query result @title Title of the audio file @performer Performer of the audio file
//@audio_url The URL of the audio file @audio_duration Audio file duration, in seconds
//@reply_markup The message reply markup. Must be of type replyMarkupInlineKeyboard or null
//@input_message_content The content of the message to be sent. Must be one of the following types: InputMessageText, InputMessageAudio, InputMessageLocation, InputMessageVenue or InputMessageContact
inputInlineQueryResultAudio id:string title:string performer:string audio_url:string audio_duration:int32 reply_markup:ReplyMarkup input_message_content:InputMessageContent = InputInlineQueryResult;

//@description Represents a user contact @id Unique identifier of the query result @contact User contact @thumbnail_url URL of the result thumbnail, if it exists @thumbnail_width Thumbnail width, if known @thumbnail_height Thumbnail height, if known
//@reply_markup The message reply markup. Must be of type replyMarkupInlineKeyboard or null
//@input_message_content The content of the message to be sent. Must be one of the following types: InputMessageText, InputMessageLocation, InputMessageVenue or InputMessageContact
inputInlineQueryResultContact id:string contact:contact thumbnail_url:string thumbnail_width:int32 thumbnail_height:int32 reply_markup:ReplyMarkup input_message_content:InputMessageContent = InputInlineQueryResult;

//@description Represents a link to a file @id Unique identifier of the query result @title Title of the resulting file @param_description Short description of the result, if known @document_url URL of the file @mime_type MIME type of the file content; only "application/pdf" and "application/zip" are currently allowed
//@thumbnail_url The URL of the file thumbnail, if it exists @thumbnail_width Width of the thumbnail @thumbnail_height Height of the thumbnail
//@reply_markup The message reply markup. Must be of type replyMarkupInlineKeyboard or null
//@input_message_content The content of the message to be sent. Must be one of the following types: InputMessageText, InputMessageDocument, InputMessageLocation, InputMessageVenue or InputMessageContact
inputInlineQueryResultDocument id:string title:string description:string document_url:string mime_type:string thumbnail_url:string thumbnail_width:int32 thumbnail_height:int32 reply_markup:ReplyMarkup input_message_content:InputMessageContent = InputInlineQueryResult;

//@description Represents a game @id Unique identifier of the query result @game_short_name Short name of the game @reply_markup Message reply markup. Must be of type replyMarkupInlineKeyboard or null
inputInlineQueryResultGame id:string game_short_name:string reply_markup:ReplyMarkup = InputInlineQueryResult;

//@description Represents a point on the map @id Unique identifier of the query result @location Location result @live_period Amount of time relative to the message sent time until the location can be updated, in seconds @title Title of the result @thumbnail_url URL of the result thumbnail, if it exists @thumbnail_width Thumbnail width, if known @thumbnail_height Thumbnail height, if known
//@reply_markup The message reply markup. Must be of type replyMarkupInlineKeyboard or null
//@input_message_content The content of the message to be sent. Must be one of the following types: InputMessageText, InputMessageLocation, InputMessageVenue or InputMessageContact
inputInlineQueryResultLocation id:string location:location live_period:int32 title:string thumbnail_url:string thumbnail_width:int32 thumbnail_height:int32 reply_markup:ReplyMarkup input_message_content:InputMessageContent = InputInlineQueryResult;

//@description Represents link to a JPEG image @id Unique identifier of the query result @title Title of the result, if known @param_description A short description of the result, if known @thumbnail_url URL of the photo thumbnail, if it exists
//@photo_url The URL of the JPEG photo (photo size must not exceed 5MB) @photo_width Width of the photo @photo_height Height of the photo
//@reply_markup The message reply markup. Must be of type replyMarkupInlineKeyboard or null
//@input_message_content The content of the message to be sent. Must be one of the following types: InputMessageText, InputMessagePhoto, InputMessageLocation, InputMessageVenue or InputMessageContact
inputInlineQueryResultPhoto id:string title:string description:string thumbnail_url:string photo_url:string photo_width:int32 photo_height:int32 reply_markup:ReplyMarkup input_message_content:InputMessageContent = InputInlineQueryResult;

//@description Represents a link to a WEBP sticker @id Unique identifier of the query result @thumbnail_url URL of the sticker thumbnail, if it exists
//@sticker_url The URL of the WEBP sticker (sticker file size must not exceed 5MB) @sticker_width Width of the sticker @sticker_height Height of the sticker
//@reply_markup The message reply markup. Must be of type replyMarkupInlineKeyboard or null
//@input_message_content The content of the message to be sent. Must be one of the following types: InputMessageText, inputMessageSticker, InputMessageLocation, InputMessageVenue or InputMessageContact
inputInlineQueryResultSticker id:string thumbnail_url:string sticker_url:string sticker_width:int32 sticker_height:int32 reply_markup:ReplyMarkup input_message_content:InputMessageContent = InputInlineQueryResult;

//@description Represents information about a venue @id Unique identifier of the query result @venue Venue result @thumbnail_url URL of the result thumbnail, if it exists @thumbnail_width Thumbnail width, if known @thumbnail_height Thumbnail height, if known
//@reply_markup The message reply markup. Must be of type replyMarkupInlineKeyboard or null
//@input_message_content The content of the message to be sent. Must be one of the following types: InputMessageText, InputMessageLocation, InputMessageVenue or InputMessageContact
inputInlineQueryResultVenue id:string venue:venue thumbnail_url:string thumbnail_width:int32 thumbnail_height:int32 reply_markup:ReplyMarkup input_message_content:InputMessageContent = InputInlineQueryResult;

//@description Represents a link to a page containing an embedded video player or a video file @id Unique identifier of the query result @title Title of the result @param_description A short description of the result, if known
//@thumbnail_url The URL of the video thumbnail (JPEG), if it exists @video_url URL of the embedded video player or video file @mime_type MIME type of the content of the video URL, only "text/html" or "video/mp4" are currently supported
//@video_width Width of the video @video_height Height of the video @video_duration Video duration, in seconds
//@reply_markup The message reply markup. Must be of type replyMarkupInlineKeyboard or null
//@input_message_content The content of the message to be sent. Must be one of the following types: InputMessageText, InputMessageVideo, InputMessageLocation, InputMessageVenue or InputMessageContact
inputInlineQueryResultVideo id:string title:string description:string thumbnail_url:string video_url:string mime_type:string video_width:int32 video_height:int32 video_duration:int32 reply_markup:ReplyMarkup input_message_content:InputMessageContent = InputInlineQueryResult;

//@description Represents a link to an opus-encoded audio file within an OGG container, single channel audio @id Unique identifier of the query result @title Title of the voice note
//@voice_note_url The URL of the voice note file @voice_note_duration Duration of the voice note, in seconds
//@reply_markup The message reply markup. Must be of type replyMarkupInlineKeyboard or null
//@input_message_content The content of the message to be sent. Must be one of the following types: InputMessageText, InputMessageVoiceNote, InputMessageLocation, InputMessageVenue or InputMessageContact
inputInlineQueryResultVoiceNote id:string title:string voice_note_url:string voice_note_duration:int32 reply_markup:ReplyMarkup input_message_content:InputMessageContent = InputInlineQueryResult;


//@class InlineQueryResult @description Represents a single result of an inline query

//@description Represents a link to an article or web page @id Unique identifier of the query result @url URL of the result, if it exists @hide_url True, if the URL must be not shown @title Title of the result
//@param_description A short description of the result @thumbnail Result thumbnail; may be null
inlineQueryResultArticle id:string url:string hide_url:Bool title:string description:string thumbnail:photoSize = InlineQueryResult;

//@description Represents a user contact @id Unique identifier of the query result @contact A user contact @thumbnail Result thumbnail; may be null
inlineQueryResultContact id:string contact:contact thumbnail:photoSize = InlineQueryResult;

//@description Represents a point on the map @id Unique identifier of the query result @location Location result @title Title of the result @thumbnail Result thumbnail; may be null
inlineQueryResultLocation id:string location:location title:string thumbnail:photoSize = InlineQueryResult;

//@description Represents information about a venue @id Unique identifier of the query result @venue Venue result @thumbnail Result thumbnail; may be null
inlineQueryResultVenue id:string venue:venue thumbnail:photoSize = InlineQueryResult;

//@description Represents information about a game @id Unique identifier of the query result @game Game result
inlineQueryResultGame id:string game:game = InlineQueryResult;

//@description Represents an animation file @id Unique identifier of the query result @animation Animation file @title Animation title
inlineQueryResultAnimation id:string animation:animation title:string = InlineQueryResult;

//@description Represents an audio file @id Unique identifier of the query result @audio Audio file
inlineQueryResultAudio id:string audio:audio = InlineQueryResult;

//@description Represents a document @id Unique identifier of the query result @document Document @title Document title @param_description Document description
inlineQueryResultDocument id:string document:document title:string description:string = InlineQueryResult;

//@description Represents a photo @id Unique identifier of the query result @photo Photo @title Title of the result, if known @param_description A short description of the result, if known
inlineQueryResultPhoto id:string photo:photo title:string description:string = InlineQueryResult;

//@description Represents a sticker @id Unique identifier of the query result @sticker Sticker
inlineQueryResultSticker id:string sticker:sticker = InlineQueryResult;

//@description Represents a video @id Unique identifier of the query result @video Video @title Title of the video @param_description Description of the video
inlineQueryResultVideo id:string video:video title:string description:string = InlineQueryResult;

//@description Represents a voice note @id Unique identifier of the query result @voice_note Voice note @title Title of the voice note
inlineQueryResultVoiceNote id:string voice_note:voiceNote title:string = InlineQueryResult;


//@description Represents the results of the inline query. Use sendInlineQueryResultMessage to send the result of the query @inline_query_id Unique identifier of the inline query @next_offset The offset for the next request. If empty, there are no more results @results Results of the query
//@switch_pm_text If non-empty, this text should be shown on the button, which opens a private chat with the bot and sends the bot a start message with the switch_pm_parameter @switch_pm_parameter Parameter for the bot start message
inlineQueryResults inline_query_id:int64 next_offset:string results:vector<InlineQueryResult> switch_pm_text:string switch_pm_parameter:string = InlineQueryResults;


//@class CallbackQueryPayload @description Represents a payload of a callback query

//@description The payload from a general callback button @data Data that was attached to the callback button
callbackQueryPayloadData data:bytes = CallbackQueryPayload;

//@description The payload from a game callback button @game_short_name A short name of the game that was attached to the callback button
callbackQueryPayloadGame game_short_name:string = CallbackQueryPayload;


//@description Contains a bot's answer to a callback query @text Text of the answer @show_alert True, if an alert should be shown to the user instead of a toast notification @url URL to be opened
callbackQueryAnswer text:string show_alert:Bool url:string = CallbackQueryAnswer;


//@description Contains the result of a custom request @result A JSON-serialized result
customRequestResult result:string = CustomRequestResult;


//@description Contains one row of the game high score table @position Position in the high score table @user_id User identifier @score User score
gameHighScore position:int32 user_id:int32 score:int32 = GameHighScore;

//@description Contains a list of game high scores @scores A list of game high scores
gameHighScores scores:vector<gameHighScore> = GameHighScores;


//@class ChatEventAction @description Represents a chat event

//@description A message was edited @old_message The original message before the edit @new_message The message after it was edited
chatEventMessageEdited old_message:message new_message:message = ChatEventAction;

//@description A message was deleted @message Deleted message
chatEventMessageDeleted message:message = ChatEventAction;

//@description A message was pinned @message Pinned message
chatEventMessagePinned message:message = ChatEventAction;

//@description A message was unpinned
chatEventMessageUnpinned = ChatEventAction;

//@description A new member joined the chat
chatEventMemberJoined = ChatEventAction;

//@description A member left the chat
chatEventMemberLeft = ChatEventAction;

//@description A new chat member was invited @user_id New member user identifier @status New member status
chatEventMemberInvited user_id:int32 status:ChatMemberStatus = ChatEventAction;

//@description A chat member has gained/lost administrator status, or the list of their administrator privileges has changed @user_id Chat member user identifier @old_status Previous status of the chat member @new_status New status of the chat member
chatEventMemberPromoted user_id:int32 old_status:ChatMemberStatus new_status:ChatMemberStatus = ChatEventAction;

//@description A chat member was restricted/unrestricted or banned/unbanned, or the list of their restrictions has changed @user_id Chat member user identifier @old_status Previous status of the chat member @new_status New status of the chat member
chatEventMemberRestricted user_id:int32 old_status:ChatMemberStatus new_status:ChatMemberStatus = ChatEventAction;

//@description The chat title was changed @old_title Previous chat title @new_title New chat title
chatEventTitleChanged old_title:string new_title:string = ChatEventAction;

//@description The chat description was changed @old_description Previous chat description @new_description New chat description
chatEventDescriptionChanged old_description:string new_description:string = ChatEventAction;

//@description The chat username was changed @old_username Previous chat username @new_username New chat username
chatEventUsernameChanged old_username:string new_username:string = ChatEventAction;

//@description The chat photo was changed @old_photo Previous chat photo value; may be null @new_photo New chat photo value; may be null
chatEventPhotoChanged old_photo:chatPhoto new_photo:chatPhoto = ChatEventAction;

//@description The anyone_can_invite setting of a supergroup chat was toggled @anyone_can_invite New value of anyone_can_invite
chatEventInvitesToggled anyone_can_invite:Bool = ChatEventAction;

//@description The sign_messages setting of a channel was toggled @sign_messages New value of sign_messages
chatEventSignMessagesToggled sign_messages:Bool = ChatEventAction;

//@description The supergroup sticker set was changed @old_sticker_set_id Previous identifier of the chat sticker set; 0 if none @new_sticker_set_id New identifier of the chat sticker set; 0 if none
chatEventStickerSetChanged old_sticker_set_id:int64 new_sticker_set_id:int64 = ChatEventAction;

//@description The is_all_history_available setting of a supergroup was toggled @is_all_history_available New value of is_all_history_available
chatEventIsAllHistoryAvailableToggled is_all_history_available:Bool = ChatEventAction;

//@description Represents a chat event @id Chat event identifier @date Point in time (Unix timestamp) when the event happened @user_id Identifier of the user who performed the action that triggered the event @action Action performed by the user
chatEvent id:int64 date:int32 user_id:int32 action:ChatEventAction = ChatEvent;

//@description Contains a list of chat events @events List of events
chatEvents events:vector<chatEvent> = ChatEvents;

//@description Represents a set of filters used to obtain a chat event log
//@message_edits True, if message edits should be returned
//@message_deletions True, if message deletions should be returned
//@message_pins True, if pin/unpin events should be returned
//@member_joins True, if members joining events should be returned
//@member_leaves True, if members leaving events should be returned
//@member_invites True, if invited member events should be returned
//@member_promotions True, if member promotion/demotion events should be returned
//@member_restrictions True, if member restricted/unrestricted/banned/unbanned events should be returned
//@info_changes True, if changes in chat information should be returned
//@setting_changes True, if changes in chat settings should be returned
chatEventLogFilters message_edits:Bool message_deletions:Bool message_pins:Bool member_joins:Bool member_leaves:Bool member_invites:Bool member_promotions:Bool member_restrictions:Bool info_changes:Bool setting_changes:Bool = ChatEventLogFilters;


//@class LanguagePackStringValue @description Represents the value of a string in a language pack

//@description An ordinary language pack string @value String value
languagePackStringValueOrdinary value:string = LanguagePackStringValue;

//@description A language pack string which has different forms based on the number of some object it mentions. See https://www.unicode.org/cldr/charts/latest/supplemental/language_plural_rules.html for more info
//@zero_value Value for zero objects @one_value Value for one object @two_value Value for two objects
//@few_value Value for few objects @many_value Value for many objects @other_value Default value
languagePackStringValuePluralized zero_value:string one_value:string two_value:string few_value:string many_value:string other_value:string = LanguagePackStringValue;

//@description A deleted language pack string, the value should be taken from the built-in english language pack
languagePackStringValueDeleted = LanguagePackStringValue;


//@description Represents one language pack string @key String key @value String value
languagePackString key:string value:LanguagePackStringValue = LanguagePackString;

//@description Contains a list of language pack strings @strings A list of language pack strings
languagePackStrings strings:vector<languagePackString> = LanguagePackStrings;

//@description Contains information about a language pack @id Unique language pack identifier
//@base_language_pack_id Identifier of a base language pack; may be empty. If a string is missed in the language pack, then it should be fetched from base language pack. Unsupported in custom language packs
//@name Language name @native_name Name of the language in that language
//@plural_code A language code to be used to apply plural forms. See https://www.unicode.org/cldr/charts/latest/supplemental/language_plural_rules.html for more info
//@is_official True, if the language pack is official @is_rtl True, if the language pack strings are RTL @is_beta True, if the language pack is a beta language pack
//@is_installed True, if the language pack is installed by the current user
//@total_string_count Total number of non-deleted strings from the language pack @translated_string_count Total number of translated strings from the language pack
//@local_string_count Total number of non-deleted strings from the language pack available locally @translation_url Link to language translation interface; empty for custom local language packs
languagePackInfo id:string base_language_pack_id:string name:string native_name:string plural_code:string is_official:Bool is_rtl:Bool is_beta:Bool is_installed:Bool total_string_count:int32 translated_string_count:int32 local_string_count:int32 translation_url:string = LanguagePackInfo;

//@description Contains information about the current localization target @language_packs List of available language packs for this application
localizationTargetInfo language_packs:vector<languagePackInfo> = LocalizationTargetInfo;


//@class DeviceToken @description Represents a data needed to subscribe for push notifications through registerDevice method. To use specific push notification service, you must specify the correct application platform and upload valid server authentication data at https://my.telegram.org

//@description A token for Firebase Cloud Messaging @token Device registration token; may be empty to de-register a device @encrypt True, if push notifications should be additionally encrypted
deviceTokenFirebaseCloudMessaging token:string encrypt:Bool = DeviceToken;

//@description A token for Apple Push Notification service @device_token Device token; may be empty to de-register a device @is_app_sandbox True, if App Sandbox is enabled
deviceTokenApplePush device_token:string is_app_sandbox:Bool = DeviceToken;

//@description A token for Apple Push Notification service VoIP notifications @device_token Device token; may be empty to de-register a device @is_app_sandbox True, if App Sandbox is enabled @encrypt True, if push notifications should be additionally encrypted
deviceTokenApplePushVoIP device_token:string is_app_sandbox:Bool encrypt:Bool = DeviceToken;

//@description A token for Windows Push Notification Services @access_token The access token that will be used to send notifications; may be empty to de-register a device
deviceTokenWindowsPush access_token:string = DeviceToken;

//@description A token for Microsoft Push Notification Service @channel_uri Push notification channel URI; may be empty to de-register a device
deviceTokenMicrosoftPush channel_uri:string = DeviceToken;

//@description A token for Microsoft Push Notification Service VoIP channel @channel_uri Push notification channel URI; may be empty to de-register a device
deviceTokenMicrosoftPushVoIP channel_uri:string = DeviceToken;

//@description A token for web Push API @endpoint Absolute URL exposed by the push service where the application server can send push messages; may be empty to de-register a device
//@p256dh_base64url Base64url-encoded P-256 elliptic curve Diffie-Hellman public key @auth_base64url Base64url-encoded authentication secret
deviceTokenWebPush endpoint:string p256dh_base64url:string auth_base64url:string = DeviceToken;

//@description A token for Simple Push API for Firefox OS @endpoint Absolute URL exposed by the push service where the application server can send push messages; may be empty to de-register a device
deviceTokenSimplePush endpoint:string = DeviceToken;

//@description A token for Ubuntu Push Client service @token Token; may be empty to de-register a device
deviceTokenUbuntuPush token:string = DeviceToken;

//@description A token for BlackBerry Push Service @token Token; may be empty to de-register a device
deviceTokenBlackBerryPush token:string = DeviceToken;

//@description A token for Tizen Push Service @reg_id Push service registration identifier; may be empty to de-register a device
deviceTokenTizenPush reg_id:string = DeviceToken;


//@description Contains a globally unique push receiver identifier, which can be used to identify which account has received a push notification @id The globally unique identifier of push notification subscription
pushReceiverId id:int64 = PushReceiverId;


//@description Contains information about a wallpaper @id Unique persistent wallpaper identifier @sizes Available variants of the wallpaper in different sizes. These photos can only be downloaded; they can't be sent in a message @color Main color of the wallpaper in RGB24 format; should be treated as background color if no photos are specified
wallpaper id:int32 sizes:vector<photoSize> color:int32 = Wallpaper;

//@description Contains a list of wallpapers @wallpapers A list of wallpapers
wallpapers wallpapers:vector<wallpaper> = Wallpapers;


//@description Contains a list of hashtags @hashtags A list of hashtags
hashtags hashtags:vector<string> = Hashtags;


//@class CheckChatUsernameResult @description Represents result of checking whether a username can be set for a chat

//@description The username can be set
checkChatUsernameResultOk = CheckChatUsernameResult;

//@description The username is invalid
checkChatUsernameResultUsernameInvalid = CheckChatUsernameResult;

//@description The username is occupied
checkChatUsernameResultUsernameOccupied = CheckChatUsernameResult;

//@description The user has too much public chats, one of them should be made private first
checkChatUsernameResultPublicChatsTooMuch = CheckChatUsernameResult;

//@description The user can't be a member of a public supergroup
checkChatUsernameResultPublicGroupsUnavailable = CheckChatUsernameResult;


//@class PushMessageContent @description Contains content of a push message notification

//@description A general message with hidden content @is_pinned True, if the message is a pinned message with the specified content
pushMessageContentHidden is_pinned:Bool = PushMessageContent;

//@description An animation message (GIF-style) @animation Message content; may be null @caption Animation caption @is_pinned True, if the message is a pinned message with the specified content
pushMessageContentAnimation animation:animation caption:string is_pinned:Bool = PushMessageContent;

//@description An audio message @audio Message content; may be null @is_pinned True, if the message is a pinned message with the specified content
pushMessageContentAudio audio:audio is_pinned:Bool = PushMessageContent;

//@description A message with a user contact @name Contact's name @is_pinned True, if the message is a pinned message with the specified content
pushMessageContentContact name:string is_pinned:Bool = PushMessageContent;

//@description A contact has registered with Telegram
pushMessageContentContactRegistered = PushMessageContent;

//@description A document message (a general file) @document Message content; may be null @is_pinned True, if the message is a pinned message with the specified content
pushMessageContentDocument document:document is_pinned:Bool = PushMessageContent;

//@description A message with a game @title Game title, empty for pinned game message @is_pinned True, if the message is a pinned message with the specified content
pushMessageContentGame title:string is_pinned:Bool = PushMessageContent;

//@description A new high score was achieved in a game @title Game title, empty for pinned message @score New score, 0 for pinned message @is_pinned True, if the message is a pinned message with the specified content
pushMessageContentGameScore title:string score:int32 is_pinned:Bool = PushMessageContent;

//@description A message with an invoice from a bot @price Product price @is_pinned True, if the message is a pinned message with the specified content
pushMessageContentInvoice price:string is_pinned:Bool = PushMessageContent;

//@description A message with a location @is_live True, if the location is live @is_pinned True, if the message is a pinned message with the specified content
pushMessageContentLocation is_live:Bool is_pinned:Bool = PushMessageContent;

//@description A photo message @photo Message content; may be null @caption Photo caption @is_secret True, if the photo is secret @is_pinned True, if the message is a pinned message with the specified content
pushMessageContentPhoto photo:photo caption:string is_secret:Bool is_pinned:Bool = PushMessageContent;

//@description A message with a poll @question Poll question @is_pinned True, if the message is a pinned message with the specified content
pushMessageContentPoll question:string is_pinned:Bool = PushMessageContent;

//@description A screenshot of a message in the chat has been taken
pushMessageContentScreenshotTaken = PushMessageContent;

//@description A message with a sticker @sticker Message content; may be null @emoji Emoji corresponding to the sticker; may be empty @is_pinned True, if the message is a pinned message with the specified content
pushMessageContentSticker sticker:sticker emoji:string is_pinned:Bool = PushMessageContent;

//@description A text message @text Message text @is_pinned True, if the message is a pinned message with the specified content
pushMessageContentText text:string is_pinned:Bool = PushMessageContent;

//@description A video message @video Message content; may be null @caption Video caption @is_secret True, if the video is secret @is_pinned True, if the message is a pinned message with the specified content
pushMessageContentVideo video:video caption:string is_secret:Bool is_pinned:Bool = PushMessageContent;

//@description A video note message @video_note Message content; may be null @is_pinned True, if the message is a pinned message with the specified content
pushMessageContentVideoNote video_note:videoNote is_pinned:Bool = PushMessageContent;

//@description A voice note message @voice_note Message content; may be null @is_pinned True, if the message is a pinned message with the specified content
pushMessageContentVoiceNote voice_note:voiceNote is_pinned:Bool = PushMessageContent;

//@description A newly created basic group
pushMessageContentBasicGroupChatCreate = PushMessageContent;

//@description New chat members were invited to a group @member_name Name of the added member @is_current_user True, if the current user was added to the group
//@is_returned True, if the user has returned to the group himself
pushMessageContentChatAddMembers member_name:string is_current_user:Bool is_returned:Bool = PushMessageContent;

//@description A chat photo was edited
pushMessageContentChatChangePhoto = PushMessageContent;

//@description A chat title was edited @title New chat title
pushMessageContentChatChangeTitle title:string = PushMessageContent;

//@description A chat member was deleted @member_name Name of the deleted member @is_current_user True, if the current user was deleted from the group
//@is_left True, if the user has left the group himself
pushMessageContentChatDeleteMember member_name:string is_current_user:Bool is_left:Bool = PushMessageContent;

//@description A new member joined the chat by invite link
pushMessageContentChatJoinByLink = PushMessageContent;

//@description A forwarded messages @total_count Number of forwarded messages
pushMessageContentMessageForwards total_count:int32 = PushMessageContent;

//@description A media album @total_count Number of messages in the album @has_photos True, if the album has at least one photo @has_videos True, if the album has at least one video
pushMessageContentMediaAlbum total_count:int32 has_photos:Bool has_videos:Bool = PushMessageContent;


//@class NotificationType @description Contains detailed information about a notification

//@description New message was received @message The message
notificationTypeNewMessage message:message = NotificationType;

//@description New secret chat was created
notificationTypeNewSecretChat = NotificationType;

//@description New call was received @call_id Call identifier
notificationTypeNewCall call_id:int32 = NotificationType;

//@description New message was received through a push notification
//@message_id The message identifier. The message will not be available in the chat history, but the ID can be used in viewMessages and as reply_to_message_id
//@sender_user_id Sender of the message. Corresponding user may be inaccessible @content Push message content
notificationTypeNewPushMessage message_id:int53 sender_user_id:int32 content:PushMessageContent = NotificationType;


//@class NotificationGroupType @description Describes type of notifications in the group

//@description A group containing notifications of type notificationTypeNewMessage and notificationTypeNewPushMessage with ordinary unread messages
notificationGroupTypeMessages = NotificationGroupType;

//@description A group containing notifications of type notificationTypeNewMessage and notificationTypeNewPushMessage with unread mentions of the current user, replies to their messages, or a pinned message
notificationGroupTypeMentions = NotificationGroupType;

//@description A group containing a notification of type notificationTypeNewSecretChat
notificationGroupTypeSecretChat = NotificationGroupType;

//@description A group containing notifications of type notificationTypeNewCall
notificationGroupTypeCalls = NotificationGroupType;


//@description Contains information about a notification @id Unique persistent identifier of this notification @date Notification date @type Notification type
notification id:int32 date:int32 type:NotificationType = Notification;

//@description Describes a group of notifications @id Unique persistent auto-incremented from 1 identifier of the notification group @type Type of the group
//@chat_id Identifier of a chat to which all notifications in the group belong
//@total_count Total number of active notifications in the group @notifications The list of active notifications
notificationGroup id:int32 type:NotificationGroupType chat_id:int53 total_count:int32 notifications:vector<notification> = NotificationGroup;


//@class OptionValue @description Represents the value of an option

//@description Represents a boolean option @value The value of the option
optionValueBoolean value:Bool = OptionValue;

//@description Represents an unknown option or an option which has a default value
optionValueEmpty = OptionValue;

//@description Represents an integer option @value The value of the option
optionValueInteger value:int32 = OptionValue;

//@description Represents a string option @value The value of the option
optionValueString value:string = OptionValue;


//@description Represents one member of a JSON object @key Member's key @value Member's value
jsonObjectMember key:string value:JsonValue = JsonObjectMember;

//@class JsonValue @description Represents a JSON value

//@description Represents a null JSON value
jsonValueNull = JsonValue;

//@description Represents a boolean JSON value @value The value
jsonValueBoolean value:Bool = JsonValue;

//@description Represents a numeric JSON value @value The value
jsonValueNumber value:double = JsonValue;

//@description Represents a string JSON value @value The value
jsonValueString value:string = JsonValue;

//@description Represents a JSON array @values The list of array elements
jsonValueArray values:vector<JsonValue> = JsonValue;

//@description Represents a JSON object @members The list of object members
jsonValueObject members:vector<jsonObjectMember> = JsonValue;


//@class UserPrivacySettingRule @description Represents a single rule for managing privacy settings

//@description A rule to allow all users to do something
userPrivacySettingRuleAllowAll = UserPrivacySettingRule;

//@description A rule to allow all of a user's contacts to do something
userPrivacySettingRuleAllowContacts = UserPrivacySettingRule;

//@description A rule to allow certain specified users to do something @user_ids The user identifiers
userPrivacySettingRuleAllowUsers user_ids:vector<int32> = UserPrivacySettingRule;

//@description A rule to restrict all users from doing something
userPrivacySettingRuleRestrictAll = UserPrivacySettingRule;

//@description A rule to restrict all contacts of a user from doing something
userPrivacySettingRuleRestrictContacts = UserPrivacySettingRule;

//@description A rule to restrict all specified users from doing something @user_ids The user identifiers
userPrivacySettingRuleRestrictUsers user_ids:vector<int32> = UserPrivacySettingRule;

//@description A list of privacy rules. Rules are matched in the specified order. The first matched rule defines the privacy setting for a given user. If no rule matches, the action is not allowed @rules A list of rules
userPrivacySettingRules rules:vector<UserPrivacySettingRule> = UserPrivacySettingRules;

//@class UserPrivacySetting @description Describes available user privacy settings

//@description A privacy setting for managing whether the user's online status is visible
userPrivacySettingShowStatus = UserPrivacySetting;

//@description A privacy setting for managing whether the user can be invited to chats
userPrivacySettingAllowChatInvites = UserPrivacySetting;

//@description A privacy setting for managing whether the user can be called
userPrivacySettingAllowCalls = UserPrivacySetting;

//@description A privacy setting for managing whether peer-to-peer connections can be used for calls
userPrivacySettingAllowPeerToPeerCalls = UserPrivacySetting;


//@description Contains information about the period of inactivity after which the current user's account will automatically be deleted @days Number of days of inactivity before the account will be flagged for deletion; should range from 30-366 days
accountTtl days:int32 = AccountTtl;


//@description Contains information about one session in a Telegram application used by the current user. Sessions should be shown to the user in the returned order
//@id Session identifier @is_current True, if this session is the current session
//@is_password_pending True, if a password is needed to complete authorization of the session
//@api_id Telegram API identifier, as provided by the application @application_name Name of the application, as provided by the application
//@application_version The version of the application, as provided by the application @is_official_application True, if the application is an official application or uses the api_id of an official application
//@device_model Model of the device the application has been run or is running on, as provided by the application @platform Operating system the application has been run or is running on, as provided by the application
//@system_version Version of the operating system the application has been run or is running on, as provided by the application @log_in_date Point in time (Unix timestamp) when the user has logged in
//@last_active_date Point in time (Unix timestamp) when the session was last used @ip IP address from which the session was created, in human-readable format
//@country A two-letter country code for the country from which the session was created, based on the IP address @region Region code from which the session was created, based on the IP address
session id:int64 is_current:Bool is_password_pending:Bool api_id:int32 application_name:string application_version:string is_official_application:Bool device_model:string platform:string system_version:string log_in_date:int32 last_active_date:int32 ip:string country:string region:string = Session;

//@description Contains a list of sessions @sessions List of sessions
sessions sessions:vector<session> = Sessions;


//@description Contains information about one website the current user is logged in with Telegram
//@id Website identifier
//@domain_name The domain name of the website
//@bot_user_id User identifier of a bot linked with the website
//@browser The version of a browser used to log in
//@platform Operating system the browser is running on
//@log_in_date Point in time (Unix timestamp) when the user was logged in
//@last_active_date Point in time (Unix timestamp) when obtained authorization was last used
//@ip IP address from which the user was logged in, in human-readable format
//@location Human-readable description of a country and a region, from which the user was logged in, based on the IP address
connectedWebsite id:int64 domain_name:string bot_user_id:int32 browser:string platform:string log_in_date:int32 last_active_date:int32 ip:string location:string = ConnectedWebsite;

//@description Contains a list of websites the current user is logged in with Telegram @websites List of connected websites
connectedWebsites websites:vector<connectedWebsite> = ConnectedWebsites;


//@description Contains information about the availability of the "Report spam" action for a chat @can_report_spam True, if a prompt with the "Report spam" action should be shown to the user
chatReportSpamState can_report_spam:Bool = ChatReportSpamState;

//@class ChatReportReason @description Describes the reason why a chat is reported

//@description The chat contains spam messages
chatReportReasonSpam = ChatReportReason;

//@description The chat promotes violence
chatReportReasonViolence = ChatReportReason;

//@description The chat contains pornographic messages
chatReportReasonPornography = ChatReportReason;

//@description The chat has child abuse related content
chatReportReasonChildAbuse = ChatReportReason;

//@description The chat contains copyrighted content
chatReportReasonCopyright = ChatReportReason;

//@description A custom reason provided by the user @text Report text
chatReportReasonCustom text:string = ChatReportReason;


//@description Contains a public HTTPS link to a message in a public supergroup or channel @link Message link @html HTML-code for embedding the message
publicMessageLink link:string html:string = PublicMessageLink;


//@description Contains a part of a file @data File bytes
filePart data:bytes = FilePart;


//@class FileType @description Represents the type of a file

//@description The data is not a file
fileTypeNone = FileType;

//@description The file is an animation
fileTypeAnimation = FileType;

//@description The file is an audio file
fileTypeAudio = FileType;

//@description The file is a document
fileTypeDocument = FileType;

//@description The file is a photo
fileTypePhoto = FileType;

//@description The file is a profile photo
fileTypeProfilePhoto = FileType;

//@description The file was sent to a secret chat (the file type is not known to the server)
fileTypeSecret = FileType;

//@description The file is a thumbnail of a file from a secret chat
fileTypeSecretThumbnail = FileType;

//@description The file is a file from Secure storage used for storing Telegram Passport files
fileTypeSecure = FileType;

//@description The file is a sticker
fileTypeSticker = FileType;

//@description The file is a thumbnail of another file
fileTypeThumbnail = FileType;

//@description The file type is not yet known
fileTypeUnknown = FileType;

//@description The file is a video
fileTypeVideo = FileType;

//@description The file is a video note
fileTypeVideoNote = FileType;

//@description The file is a voice note
fileTypeVoiceNote = FileType;

//@description The file is a wallpaper
fileTypeWallpaper = FileType;


//@description Contains the storage usage statistics for a specific file type @file_type File type @size Total size of the files @count Total number of files
storageStatisticsByFileType file_type:FileType size:int53 count:int32 = StorageStatisticsByFileType;

//@description Contains the storage usage statistics for a specific chat @chat_id Chat identifier; 0 if none @size Total size of the files in the chat @count Total number of files in the chat @by_file_type Statistics split by file types
storageStatisticsByChat chat_id:int53 size:int53 count:int32 by_file_type:vector<storageStatisticsByFileType> = StorageStatisticsByChat;

//@description Contains the exact storage usage statistics split by chats and file type @size Total size of files @count Total number of files @by_chat Statistics split by chats
storageStatistics size:int53 count:int32 by_chat:vector<storageStatisticsByChat> = StorageStatistics;

//@description Contains approximate storage usage statistics, excluding files of unknown file type @files_size Approximate total size of files @file_count Approximate number of files
//@database_size Size of the database @language_pack_database_size Size of the language pack database @log_size Size of the TDLib internal log
storageStatisticsFast files_size:int53 file_count:int32 database_size:int53 language_pack_database_size:int53 log_size:int53 = StorageStatisticsFast;

//@description Contains database statistics
//@statistics Database statistics in an unspecified human-readable format
databaseStatistics statistics:string = DatabaseStatistics;


//@class NetworkType @description Represents the type of a network

//@description The network is not available
networkTypeNone = NetworkType;

//@description A mobile network
networkTypeMobile = NetworkType;

//@description A mobile roaming network
networkTypeMobileRoaming = NetworkType;

//@description A Wi-Fi network
networkTypeWiFi = NetworkType;

//@description A different network type (e.g., Ethernet network)
networkTypeOther = NetworkType;


//@class NetworkStatisticsEntry @description Contains statistics about network usage

//@description Contains information about the total amount of data that was used to send and receive files @file_type Type of the file the data is part of @network_type Type of the network the data was sent through. Call setNetworkType to maintain the actual network type
//@sent_bytes Total number of bytes sent @received_bytes Total number of bytes received
networkStatisticsEntryFile file_type:FileType network_type:NetworkType sent_bytes:int53 received_bytes:int53 = NetworkStatisticsEntry;

//@description Contains information about the total amount of data that was used for calls @network_type Type of the network the data was sent through. Call setNetworkType to maintain the actual network type
//@sent_bytes Total number of bytes sent @received_bytes Total number of bytes received @duration Total call duration, in seconds
networkStatisticsEntryCall network_type:NetworkType sent_bytes:int53 received_bytes:int53 duration:double = NetworkStatisticsEntry;

//@description A full list of available network statistic entries @since_date Point in time (Unix timestamp) when the app began collecting statistics @entries Network statistics entries
networkStatistics since_date:int32 entries:vector<NetworkStatisticsEntry> = NetworkStatistics;


//@class ConnectionState @description Describes the current state of the connection to Telegram servers

//@description Currently waiting for the network to become available. Use SetNetworkType to change the available network type
connectionStateWaitingForNetwork = ConnectionState;

//@description Currently establishing a connection with a proxy server
connectionStateConnectingToProxy = ConnectionState;

//@description Currently establishing a connection to the Telegram servers
connectionStateConnecting = ConnectionState;

//@description Downloading data received while the client was offline
connectionStateUpdating = ConnectionState;

//@description There is a working connection to the Telegram servers
connectionStateReady = ConnectionState;


//@class TopChatCategory @description Represents the categories of chats for which a list of frequently used chats can be retrieved

//@description A category containing frequently used private chats with non-bot users
topChatCategoryUsers = TopChatCategory;

//@description A category containing frequently used private chats with bot users
topChatCategoryBots = TopChatCategory;

//@description A category containing frequently used basic groups and supergroups
topChatCategoryGroups = TopChatCategory;

//@description A category containing frequently used channels
topChatCategoryChannels = TopChatCategory;

//@description A category containing frequently used chats with inline bots sorted by their usage in inline mode
topChatCategoryInlineBots = TopChatCategory;

//@description A category containing frequently used chats used for calls
topChatCategoryCalls = TopChatCategory;


//@class TMeUrlType @description Describes the type of a URL linking to an internal Telegram entity

//@description A URL linking to a user @user_id Identifier of the user
tMeUrlTypeUser user_id:int32 = TMeUrlType;

//@description A URL linking to a public supergroup or channel @supergroup_id Identifier of the supergroup or channel
tMeUrlTypeSupergroup supergroup_id:int53 = TMeUrlType;

//@description A chat invite link @info Chat invite link info
tMeUrlTypeChatInvite info:chatInviteLinkInfo = TMeUrlType;

//@description A URL linking to a sticker set @sticker_set_id Identifier of the sticker set
tMeUrlTypeStickerSet sticker_set_id:int64 = TMeUrlType;

//@description Represents a URL linking to an internal Telegram entity @url URL @type Type of the URL
tMeUrl url:string type:TMeUrlType = TMeUrl;

//@description Contains a list of t.me URLs @urls List of URLs
tMeUrls urls:vector<tMeUrl> = TMeUrls;


//@description Contains a counter @count Count
count count:int32 = Count;

//@description Contains some text @text Text
text text:string = Text;

//@description Contains a value representing a number of seconds @seconds Number of seconds
seconds seconds:double = Seconds;


//@description Contains information about a tg:// deep link @text Text to be shown to the user @need_update_application True, if user should be asked to update the application
deepLinkInfo text:formattedText need_update_application:Bool = DeepLinkInfo;


//@class TextParseMode @description Describes the way the text should be parsed for TextEntities

//@description The text should be parsed in markdown-style
textParseModeMarkdown = TextParseMode;

//@description The text should be parsed in HTML-style
textParseModeHTML = TextParseMode;


//@class ProxyType @description Describes the type of the proxy server

//@description A SOCKS5 proxy server @username Username for logging in; may be empty @password Password for logging in; may be empty
proxyTypeSocks5 username:string password:string = ProxyType;

//@description A HTTP transparent proxy server @username Username for logging in; may be empty @password Password for logging in; may be empty @http_only Pass true, if the proxy supports only HTTP requests and doesn't support transparent TCP connections via HTTP CONNECT method
proxyTypeHttp username:string password:string http_only:Bool = ProxyType;

//@description An MTProto proxy server @secret The proxy's secret in hexadecimal encoding
proxyTypeMtproto secret:string = ProxyType;


//@description Contains information about a proxy server @id Unique identifier of the proxy @server Proxy server IP address @port Proxy server port @last_used_date Point in time (Unix timestamp) when the proxy was last used; 0 if never @is_enabled True, if the proxy is enabled now @type Type of the proxy
proxy id:int32 server:string port:int32 last_used_date:int32 is_enabled:Bool type:ProxyType = Proxy;

//@description Represents a list of proxy servers @proxies List of proxy servers
proxies proxies:vector<proxy> = Proxies;


//@description Describes a sticker that should be added to a sticker set @png_sticker PNG image with the sticker; must be up to 512 kB in size and fit in a 512x512 square @emojis Emoji corresponding to the sticker @mask_position For masks, position where the mask should be placed; may be null
inputSticker png_sticker:InputFile emojis:string mask_position:maskPosition = InputSticker;


//@class Update @description Contains notifications about data changes

//@description The user authorization state has changed @authorization_state New authorization state
updateAuthorizationState authorization_state:AuthorizationState = Update;

//@description A new message was received; can also be an outgoing message @message The new message
updateNewMessage message:message = Update;

//@description A request to send a message has reached the Telegram server. This doesn't mean that the message will be sent successfully or even that the send message request will be processed. This update will be sent only if the option "use_quick_ack" is set to true. This update may be sent multiple times for the same message
//@chat_id The chat identifier of the sent message @message_id A temporary message identifier
updateMessageSendAcknowledged chat_id:int53 message_id:int53 = Update;

//@description A message has been successfully sent @message Information about the sent message. Usually only the message identifier, date, and content are changed, but almost all other fields can also change @old_message_id The previous temporary message identifier
updateMessageSendSucceeded message:message old_message_id:int53 = Update;

//@description A message failed to send. Be aware that some messages being sent can be irrecoverably deleted, in which case updateDeleteMessages will be received instead of this update
//@message Contains information about the message that failed to send @old_message_id The previous temporary message identifier @error_code An error code @error_message Error message
updateMessageSendFailed message:message old_message_id:int53 error_code:int32 error_message:string = Update;

//@description The message content has changed @chat_id Chat identifier @message_id Message identifier @new_content New message content
updateMessageContent chat_id:int53 message_id:int53 new_content:MessageContent = Update;

//@description A message was edited. Changes in the message content will come in a separate updateMessageContent @chat_id Chat identifier @message_id Message identifier @edit_date Point in time (Unix timestamp) when the message was edited @reply_markup New message reply markup; may be null
updateMessageEdited chat_id:int53 message_id:int53 edit_date:int32 reply_markup:ReplyMarkup = Update;

//@description The view count of the message has changed @chat_id Chat identifier @message_id Message identifier @views New value of the view count
updateMessageViews chat_id:int53 message_id:int53 views:int32 = Update;

//@description The message content was opened. Updates voice note messages to "listened", video note messages to "viewed" and starts the TTL timer for self-destructing messages @chat_id Chat identifier @message_id Message identifier
updateMessageContentOpened chat_id:int53 message_id:int53 = Update;

//@description A message with an unread mention was read @chat_id Chat identifier @message_id Message identifier @unread_mention_count The new number of unread mention messages left in the chat
updateMessageMentionRead chat_id:int53 message_id:int53 unread_mention_count:int32 = Update;

//@description A new chat has been loaded/created. This update is guaranteed to come before the chat identifier is returned to the client. The chat field changes will be reported through separate updates @chat The chat
updateNewChat chat:chat = Update;

//@description The title of a chat was changed @chat_id Chat identifier @title The new chat title
updateChatTitle chat_id:int53 title:string = Update;

//@description A chat photo was changed @chat_id Chat identifier @photo The new chat photo; may be null
updateChatPhoto chat_id:int53 photo:chatPhoto = Update;

//@description The last message of a chat was changed. If last_message is null then the last message in the chat became unknown. Some new unknown messages might be added to the chat in this case @chat_id Chat identifier @last_message The new last message in the chat; may be null @order New value of the chat order
updateChatLastMessage chat_id:int53 last_message:message order:int64 = Update;

//@description The order of the chat in the chat list has changed. Instead of this update updateChatLastMessage, updateChatIsPinned or updateChatDraftMessage might be sent @chat_id Chat identifier @order New value of the order
updateChatOrder chat_id:int53 order:int64 = Update;

//@description A chat was pinned or unpinned @chat_id Chat identifier @is_pinned New value of is_pinned @order New value of the chat order
updateChatIsPinned chat_id:int53 is_pinned:Bool order:int64 = Update;

//@description A chat was marked as unread or was read @chat_id Chat identifier @is_marked_as_unread New value of is_marked_as_unread
updateChatIsMarkedAsUnread chat_id:int53 is_marked_as_unread:Bool = Update;

//@description A chat's is_sponsored field has changed @chat_id Chat identifier @is_sponsored New value of is_sponsored @order New value of chat order
updateChatIsSponsored chat_id:int53 is_sponsored:Bool order:int64 = Update;

//@description The value of the default disable_notification parameter, used when a message is sent to the chat, was changed @chat_id Chat identifier @default_disable_notification The new default_disable_notification value
updateChatDefaultDisableNotification chat_id:int53 default_disable_notification:Bool = Update;

//@description Incoming messages were read or number of unread messages has been changed @chat_id Chat identifier @last_read_inbox_message_id Identifier of the last read incoming message @unread_count The number of unread messages left in the chat
updateChatReadInbox chat_id:int53 last_read_inbox_message_id:int53 unread_count:int32 = Update;

//@description Outgoing messages were read @chat_id Chat identifier @last_read_outbox_message_id Identifier of last read outgoing message
updateChatReadOutbox chat_id:int53 last_read_outbox_message_id:int53 = Update;

//@description The chat unread_mention_count has changed @chat_id Chat identifier @unread_mention_count The number of unread mention messages left in the chat
updateChatUnreadMentionCount chat_id:int53 unread_mention_count:int32 = Update;

//@description Notification settings for a chat were changed @chat_id Chat identifier @notification_settings The new notification settings
updateChatNotificationSettings chat_id:int53 notification_settings:chatNotificationSettings = Update;

//@description Notification settings for some type of chats were updated @scope Types of chats for which notification settings were updated @notification_settings The new notification settings
updateScopeNotificationSettings scope:NotificationSettingsScope notification_settings:scopeNotificationSettings = Update;

//@description The chat pinned message was changed @chat_id Chat identifier @pinned_message_id The new identifier of the pinned message; 0 if there is no pinned message in the chat
updateChatPinnedMessage chat_id:int53 pinned_message_id:int53 = Update;

//@description The default chat reply markup was changed. Can occur because new messages with reply markup were received or because an old reply markup was hidden by the user
//@chat_id Chat identifier @reply_markup_message_id Identifier of the message from which reply markup needs to be used; 0 if there is no default custom reply markup in the chat
updateChatReplyMarkup chat_id:int53 reply_markup_message_id:int53 = Update;

//@description A chat draft has changed. Be aware that the update may come in the currently opened chat but with old content of the draft. If the user has changed the content of the draft, this update shouldn't be applied @chat_id Chat identifier @draft_message The new draft message; may be null @order New value of the chat order
updateChatDraftMessage chat_id:int53 draft_message:draftMessage order:int64 = Update;

//@description The number of online group members has changed. This update with non-zero count is sent only for currently opened chats. There is no guarantee that it will be sent just after the count has changed @chat_id Identifier of the chat @online_member_count New number of online members in the chat, or 0 if unknown
updateChatOnlineMemberCount chat_id:int53 online_member_count:int32 = Update;

//@description A notification was changed @notification_group_id Unique notification group identifier @notification Changed notification
updateNotification notification_group_id:int32 notification:notification = Update;

//@description A list of active notifications in a notification group has changed
//@notification_group_id Unique notification group identifier
//@type New type of the notification group
//@chat_id Identifier of a chat to which all notifications in the group belong
//@notification_settings_chat_id Chat identifier, which notification settings must be applied to the added notifications
//@is_silent True, if the notifications should be shown without sound
//@total_count Total number of unread notifications in the group, can be bigger than number of active notifications
//@added_notifications List of added group notifications, sorted by notification ID @removed_notification_ids Identifiers of removed group notifications, sorted by notification ID
updateNotificationGroup notification_group_id:int32 type:NotificationGroupType chat_id:int53 notification_settings_chat_id:int53 is_silent:Bool total_count:int32 added_notifications:vector<notification> removed_notification_ids:vector<int32> = Update;

//@description Contains active notifications that was shown on previous application launches. This update is sent only if a message database is used. In that case it comes once before any updateNotification and updateNotificationGroup update @groups Lists of active notification groups
updateActiveNotifications groups:vector<notificationGroup> = Update;

//@description Describes, whether there are some pending notification updates. Can be used to prevent application from killing, while there are some pending notifications
//@have_delayed_notifications True, if there are some delayed notification updates, which will be sent soon
//@have_unreceived_notifications True, if there can be some yet unreceived notifications, which are being fetched from the server
updateHavePendingNotifications have_delayed_notifications:Bool have_unreceived_notifications:Bool = Update;

//@description Some messages were deleted @chat_id Chat identifier @message_ids Identifiers of the deleted messages
//@is_permanent True, if the messages are permanently deleted by a user (as opposed to just becoming inaccessible)
//@from_cache True, if the messages are deleted only from the cache and can possibly be retrieved again in the future
updateDeleteMessages chat_id:int53 message_ids:vector<int53> is_permanent:Bool from_cache:Bool = Update;

//@description User activity in the chat has changed @chat_id Chat identifier @user_id Identifier of a user performing an action @action The action description
updateUserChatAction chat_id:int53 user_id:int32 action:ChatAction = Update;

//@description The user went online or offline @user_id User identifier @status New status of the user
updateUserStatus user_id:int32 status:UserStatus = Update;

//@description Some data of a user has changed. This update is guaranteed to come before the user identifier is returned to the client @user New data about the user
updateUser user:user = Update;

//@description Some data of a basic group has changed. This update is guaranteed to come before the basic group identifier is returned to the client @basic_group New data about the group
updateBasicGroup basic_group:basicGroup = Update;

//@description Some data of a supergroup or a channel has changed. This update is guaranteed to come before the supergroup identifier is returned to the client @supergroup New data about the supergroup
updateSupergroup supergroup:supergroup = Update;

//@description Some data of a secret chat has changed. This update is guaranteed to come before the secret chat identifier is returned to the client @secret_chat New data about the secret chat
updateSecretChat secret_chat:secretChat = Update;

//@description Some data from userFullInfo has been changed @user_id User identifier @user_full_info New full information about the user
updateUserFullInfo user_id:int32 user_full_info:userFullInfo = Update;

//@description Some data from basicGroupFullInfo has been changed @basic_group_id Identifier of a basic group @basic_group_full_info New full information about the group
updateBasicGroupFullInfo basic_group_id:int32 basic_group_full_info:basicGroupFullInfo = Update;

//@description Some data from supergroupFullInfo has been changed @supergroup_id Identifier of the supergroup or channel @supergroup_full_info New full information about the supergroup
updateSupergroupFullInfo supergroup_id:int32 supergroup_full_info:supergroupFullInfo = Update;

//@description Service notification from the server. Upon receiving this the client must show a popup with the content of the notification
//@type Notification type. If type begins with "AUTH_KEY_DROP_", then two buttons "Cancel" and "Log out" should be shown under notification; if user presses the second, all local data should be destroyed using Destroy method
//@content Notification content
updateServiceNotification type:string content:MessageContent = Update;

//@description Information about a file was updated @file New data about the file
updateFile file:file = Update;

//@description The file generation process needs to be started by the client
//@generation_id Unique identifier for the generation process
//@original_path The path to a file from which a new file is generated; may be empty
//@destination_path The path to a file that should be created and where the new file should be generated
//@conversion String specifying the conversion applied to the original file. If conversion is "#url#" than original_path contains an HTTP/HTTPS URL of a file, which should be downloaded by the client
updateFileGenerationStart generation_id:int64 original_path:string destination_path:string conversion:string = Update;

//@description File generation is no longer needed @generation_id Unique identifier for the generation process
updateFileGenerationStop generation_id:int64 = Update;

//@description New call was created or information about a call was updated @call New data about a call
updateCall call:call = Update;

//@description Some privacy setting rules have been changed @setting The privacy setting @rules New privacy rules
updateUserPrivacySettingRules setting:UserPrivacySetting rules:userPrivacySettingRules = Update;

//@description Number of unread messages has changed. This update is sent only if a message database is used @unread_count Total number of unread messages @unread_unmuted_count Total number of unread messages in unmuted chats
updateUnreadMessageCount unread_count:int32 unread_unmuted_count:int32 = Update;

//@description Number of unread chats, i.e. with unread messages or marked as unread, has changed. This update is sent only if a message database is used
//@unread_count Total number of unread chats @unread_unmuted_count Total number of unread unmuted chats
//@marked_as_unread_count Total number of chats marked as unread @marked_as_unread_unmuted_count Total number of unmuted chats marked as unread
updateUnreadChatCount unread_count:int32 unread_unmuted_count:int32 marked_as_unread_count:int32 marked_as_unread_unmuted_count:int32 = Update;

//@description An option changed its value @name The option name @value The new option value
updateOption name:string value:OptionValue = Update;

//@description The list of installed sticker sets was updated @is_masks True, if the list of installed mask sticker sets was updated @sticker_set_ids The new list of installed ordinary sticker sets
updateInstalledStickerSets is_masks:Bool sticker_set_ids:vector<int64> = Update;

//@description The list of trending sticker sets was updated or some of them were viewed @sticker_sets The new list of trending sticker sets
updateTrendingStickerSets sticker_sets:stickerSets = Update;

//@description The list of recently used stickers was updated @is_attached True, if the list of stickers attached to photo or video files was updated, otherwise the list of sent stickers is updated @sticker_ids The new list of file identifiers of recently used stickers
updateRecentStickers is_attached:Bool sticker_ids:vector<int32> = Update;

//@description The list of favorite stickers was updated @sticker_ids The new list of file identifiers of favorite stickers
updateFavoriteStickers sticker_ids:vector<int32> = Update;

//@description The list of saved animations was updated @animation_ids The new list of file identifiers of saved animations
updateSavedAnimations animation_ids:vector<int32> = Update;

//@description Some language pack strings have been updated @localization_target Localization target to which the language pack belongs @language_pack_id Identifier of the updated language pack @strings List of changed language pack strings
updateLanguagePackStrings localization_target:string language_pack_id:string strings:vector<languagePackString> = Update;

//@description The connection state has changed @state The new connection state
updateConnectionState state:ConnectionState = Update;

//@description New terms of service must be accepted by the user. If the terms of service are declined, then the deleteAccount method should be called with the reason "Decline ToS update" @terms_of_service_id Identifier of the terms of service @terms_of_service The new terms of service
updateTermsOfService terms_of_service_id:string terms_of_service:termsOfService = Update;

//@description A new incoming inline query; for bots only @id Unique query identifier @sender_user_id Identifier of the user who sent the query @user_location User location, provided by the client; may be null @query Text of the query @offset Offset of the first entry to return
updateNewInlineQuery id:int64 sender_user_id:int32 user_location:location query:string offset:string = Update;

//@description The user has chosen a result of an inline query; for bots only @sender_user_id Identifier of the user who sent the query @user_location User location, provided by the client; may be null @query Text of the query @result_id Identifier of the chosen result @inline_message_id Identifier of the sent inline message, if known
updateNewChosenInlineResult sender_user_id:int32 user_location:location query:string result_id:string inline_message_id:string = Update;

//@description A new incoming callback query; for bots only @id Unique query identifier @sender_user_id Identifier of the user who sent the query @chat_id Identifier of the chat, in which the query was sent
//@message_id Identifier of the message, from which the query originated @chat_instance Identifier that uniquely corresponds to the chat to which the message was sent @payload Query payload
updateNewCallbackQuery id:int64 sender_user_id:int32 chat_id:int53 message_id:int53 chat_instance:int64 payload:CallbackQueryPayload = Update;

//@description A new incoming callback query from a message sent via a bot; for bots only @id Unique query identifier @sender_user_id Identifier of the user who sent the query @inline_message_id Identifier of the inline message, from which the query originated
//@chat_instance An identifier uniquely corresponding to the chat a message was sent to @payload Query payload
updateNewInlineCallbackQuery id:int64 sender_user_id:int32 inline_message_id:string chat_instance:int64 payload:CallbackQueryPayload = Update;

//@description A new incoming shipping query; for bots only. Only for invoices with flexible price @id Unique query identifier @sender_user_id Identifier of the user who sent the query @invoice_payload Invoice payload @shipping_address User shipping address
updateNewShippingQuery id:int64 sender_user_id:int32 invoice_payload:string shipping_address:address = Update;

//@description A new incoming pre-checkout query; for bots only. Contains full information about a checkout @id Unique query identifier @sender_user_id Identifier of the user who sent the query @currency Currency for the product price @total_amount Total price for the product, in the minimal quantity of the currency
//@invoice_payload Invoice payload @shipping_option_id Identifier of a shipping option chosen by the user; may be empty if not applicable @order_info Information about the order; may be null
updateNewPreCheckoutQuery id:int64 sender_user_id:int32 currency:string total_amount:int53 invoice_payload:bytes shipping_option_id:string order_info:orderInfo = Update;

//@description A new incoming event; for bots only @event A JSON-serialized event
updateNewCustomEvent event:string = Update;

//@description A new incoming query; for bots only @id The query identifier @data JSON-serialized query data @timeout Query timeout
updateNewCustomQuery id:int64 data:string timeout:int32 = Update;

//@description Information about a poll was updated; for bots only @poll New data about the poll
updatePoll poll:poll = Update;


//@description Contains a list of updates @updates List of updates
updates updates:vector<Update> = Updates;


//@class LogStream @description Describes a stream to which TDLib internal log is written

//@description The log is written to stderr or an OS specific log
logStreamDefault = LogStream;

//@description The log is written to a file @path Path to the file to where the internal TDLib log will be written @max_file_size Maximum size of the file to where the internal TDLib log is written before the file will be auto-rotated
logStreamFile path:string max_file_size:int53 = LogStream;

//@description The log is written nowhere
logStreamEmpty = LogStream;


//@description Contains a TDLib internal log verbosity level @verbosity_level Log verbosity level
logVerbosityLevel verbosity_level:int32 = LogVerbosityLevel;

//@description Contains a list of available TDLib internal log tags @tags List of log tags
logTags tags:vector<string> = LogTags;


//@description A simple object containing a number; for testing only @value Number
testInt value:int32 = TestInt;
//@description A simple object containing a string; for testing only @value String
testString value:string = TestString;
//@description A simple object containing a sequence of bytes; for testing only @value Bytes
testBytes value:bytes = TestBytes;
//@description A simple object containing a vector of numbers; for testing only @value Vector of numbers
testVectorInt value:vector<int32> = TestVectorInt;
//@description A simple object containing a vector of objects that hold a number; for testing only @value Vector of objects
testVectorIntObject value:vector<testInt> = TestVectorIntObject;
//@description A simple object containing a vector of strings; for testing only @value Vector of strings
testVectorString value:vector<string> = TestVectorString;
//@description A simple object containing a vector of objects that hold a string; for testing only @value Vector of objects
testVectorStringObject value:vector<testString> = TestVectorStringObject;

---functions---

//@description Returns the current authorization state; this is an offline request. For informational purposes only. Use updateAuthorizationState instead to maintain the current authorization state
getAuthorizationState = AuthorizationState;


//@description Sets the parameters for TDLib initialization. Works only when the current authorization state is authorizationStateWaitTdlibParameters @parameters Parameters
setTdlibParameters parameters:tdlibParameters = Ok;

//@description Checks the database encryption key for correctness. Works only when the current authorization state is authorizationStateWaitEncryptionKey @encryption_key Encryption key to check or set up
checkDatabaseEncryptionKey encryption_key:bytes = Ok;

//@description Sets the phone number of the user and sends an authentication code to the user. Works only when the current authorization state is authorizationStateWaitPhoneNumber
//@phone_number The phone number of the user, in international format @allow_flash_call Pass true if the authentication code may be sent via flash call to the specified phone number @is_current_phone_number Pass true if the phone number is used on the current device. Ignored if allow_flash_call is false
setAuthenticationPhoneNumber phone_number:string allow_flash_call:Bool is_current_phone_number:Bool = Ok;

//@description Re-sends an authentication code to the user. Works only when the current authorization state is authorizationStateWaitCode and the next_code_type of the result is not null
resendAuthenticationCode = Ok;

//@description Checks the authentication code. Works only when the current authorization state is authorizationStateWaitCode @code The verification code received via SMS, Telegram message, phone call, or flash call
//@first_name If the user is not yet registered, the first name of the user; 1-64 characters. You can also pass an empty string for unregistered user there to check verification code validness. In the latter case PHONE_NUMBER_UNOCCUPIED error will be returned for a valid code
//@last_name If the user is not yet registered; the last name of the user; optional; 0-64 characters
checkAuthenticationCode code:string first_name:string last_name:string = Ok;

//@description Checks the authentication password for correctness. Works only when the current authorization state is authorizationStateWaitPassword @password The password to check
checkAuthenticationPassword password:string = Ok;

//@description Requests to send a password recovery code to an email address that was previously set up. Works only when the current authorization state is authorizationStateWaitPassword
requestAuthenticationPasswordRecovery = Ok;

//@description Recovers the password with a password recovery code sent to an email address that was previously set up. Works only when the current authorization state is authorizationStateWaitPassword @recovery_code Recovery code to check
recoverAuthenticationPassword recovery_code:string = Ok;

//@description Checks the authentication token of a bot; to log in as a bot. Works only when the current authorization state is authorizationStateWaitPhoneNumber. Can be used instead of setAuthenticationPhoneNumber and checkAuthenticationCode to log in @token The bot token
checkAuthenticationBotToken token:string = Ok;

//@description Closes the TDLib instance after a proper logout. Requires an available network connection. All local data will be destroyed. After the logout completes, updateAuthorizationState with authorizationStateClosed will be sent
logOut = Ok;

//@description Closes the TDLib instance. All databases will be flushed to disk and properly closed. After the close completes, updateAuthorizationState with authorizationStateClosed will be sent
close = Ok;

//@description Closes the TDLib instance, destroying all local data without a proper logout. The current user session will remain in the list of all active sessions. All local data will be destroyed. After the destruction completes updateAuthorizationState with authorizationStateClosed will be sent
destroy = Ok;


//@description Returns all updates needed to restore current TDLib state, i.e. all actual UpdateAuthorizationState/UpdateUser/UpdateNewChat and others. This is especially usefull if TDLib is run in a separate process. This is an offline method. Can be called before authorization
getCurrentState = Updates;


//@description Changes the database encryption key. Usually the encryption key is never changed and is stored in some OS keychain @new_encryption_key New encryption key
setDatabaseEncryptionKey new_encryption_key:bytes = Ok;


//@description Returns the current state of 2-step verification
getPasswordState = PasswordState;

//@description Changes the password for the user. If a new recovery email address is specified, then the change will not be applied until the new recovery email address is confirmed
//@old_password Previous password of the user @new_password New password of the user; may be empty to remove the password @new_hint New password hint; may be empty @set_recovery_email_address Pass true if the recovery email address should be changed @new_recovery_email_address New recovery email address; may be empty
setPassword old_password:string new_password:string new_hint:string set_recovery_email_address:Bool new_recovery_email_address:string = PasswordState;

//@description Returns a 2-step verification recovery email address that was previously set up. This method can be used to verify a password provided by the user @password The password for the current user
getRecoveryEmailAddress password:string = RecoveryEmailAddress;

//@description Changes the 2-step verification recovery email address of the user. If a new recovery email address is specified, then the change will not be applied until the new recovery email address is confirmed
//-If new_recovery_email_address is the same as the email address that is currently set up, this call succeeds immediately and aborts all other requests waiting for an email confirmation @password Password of the current user @new_recovery_email_address New recovery email address
setRecoveryEmailAddress password:string new_recovery_email_address:string = PasswordState;

//@description Checks the 2-step verification recovery email address verification code @code Verification code
checkRecoveryEmailAddressCode code:string = PasswordState;

//@description Resends the 2-step verification recovery email address verification code
resendRecoveryEmailAddressCode = PasswordState;

//@description Requests to send a password recovery code to an email address that was previously set up
requestPasswordRecovery = EmailAddressAuthenticationCodeInfo;

//@description Recovers the password using a recovery code sent to an email address that was previously set up @recovery_code Recovery code to check
recoverPassword recovery_code:string = PasswordState;

//@description Creates a new temporary password for processing payments @password Persistent user password @valid_for Time during which the temporary password will be valid, in seconds; should be between 60 and 86400
createTemporaryPassword password:string valid_for:int32 = TemporaryPasswordState;

//@description Returns information about the current temporary password
getTemporaryPasswordState = TemporaryPasswordState;


//@description Returns the current user
getMe = User;

//@description Returns information about a user by their identifier. This is an offline request if the current user is not a bot @user_id User identifier
getUser user_id:int32 = User;

//@description Returns full information about a user by their identifier @user_id User identifier
getUserFullInfo user_id:int32 = UserFullInfo;

//@description Returns information about a basic group by its identifier. This is an offline request if the current user is not a bot @basic_group_id Basic group identifier
getBasicGroup basic_group_id:int32 = BasicGroup;

//@description Returns full information about a basic group by its identifier @basic_group_id Basic group identifier
getBasicGroupFullInfo basic_group_id:int32 = BasicGroupFullInfo;

//@description Returns information about a supergroup or channel by its identifier. This is an offline request if the current user is not a bot @supergroup_id Supergroup or channel identifier
getSupergroup supergroup_id:int32 = Supergroup;

//@description Returns full information about a supergroup or channel by its identifier, cached for up to 1 minute @supergroup_id Supergroup or channel identifier
getSupergroupFullInfo supergroup_id:int32 = SupergroupFullInfo;

//@description Returns information about a secret chat by its identifier. This is an offline request @secret_chat_id Secret chat identifier
getSecretChat secret_chat_id:int32 = SecretChat;

//@description Returns information about a chat by its identifier, this is an offline request if the current user is not a bot @chat_id Chat identifier
getChat chat_id:int53 = Chat;

//@description Returns information about a message @chat_id Identifier of the chat the message belongs to @message_id Identifier of the message to get
getMessage chat_id:int53 message_id:int53 = Message;

//@description Returns information about a message, if it is available locally without sending network request. This is an offline request @chat_id Identifier of the chat the message belongs to @message_id Identifier of the message to get
getMessageLocally chat_id:int53 message_id:int53 = Message;

//@description Returns information about a message that is replied by given message @chat_id Identifier of the chat the message belongs to @message_id Identifier of the message reply to which get
getRepliedMessage chat_id:int53 message_id:int53 = Message;

//@description Returns information about a pinned chat message @chat_id Identifier of the chat the message belongs to
getChatPinnedMessage chat_id:int53 = Message;

//@description Returns information about messages. If a message is not found, returns null on the corresponding position of the result @chat_id Identifier of the chat the messages belong to @message_ids Identifiers of the messages to get
getMessages chat_id:int53 message_ids:vector<int53> = Messages;

//@description Returns information about a file; this is an offline request @file_id Identifier of the file to get
getFile file_id:int32 = File;

//@description Returns information about a file by its remote ID; this is an offline request. Can be used to register a URL as a file for further uploading, or sending as a message @remote_file_id Remote identifier of the file to get @file_type File type, if known
getRemoteFile remote_file_id:string file_type:FileType = File;

//@description Returns an ordered list of chats. Chats are sorted by the pair (order, chat_id) in decreasing order. (For example, to get a list of chats from the beginning, the offset_order should be equal to a biggest signed 64-bit number 9223372036854775807 == 2^63 - 1).
//-For optimal performance the number of returned chats is chosen by the library. @offset_order Chat order to return chats from @offset_chat_id Chat identifier to return chats from
//@limit The maximum number of chats to be returned. It is possible that fewer chats than the limit are returned even if the end of the list is not reached
getChats offset_order:int64 offset_chat_id:int53 limit:int32 = Chats;

//@description Searches a public chat by its username. Currently only private chats, supergroups and channels can be public. Returns the chat if found; otherwise an error is returned @username Username to be resolved
searchPublicChat username:string = Chat;

//@description Searches public chats by looking for specified query in their username and title. Currently only private chats, supergroups and channels can be public. Returns a meaningful number of results. Returns nothing if the length of the searched username prefix is less than 5. Excludes private chats with contacts and chats from the chat list from the results @query Query to search for
searchPublicChats query:string = Chats;

//@description Searches for the specified query in the title and username of already known chats, this is an offline request. Returns chats in the order seen in the chat list @query Query to search for. If the query is empty, returns up to 20 recently found chats @limit Maximum number of chats to be returned
searchChats query:string limit:int32 = Chats;

//@description Searches for the specified query in the title and username of already known chats via request to the server. Returns chats in the order seen in the chat list @query Query to search for @limit Maximum number of chats to be returned
searchChatsOnServer query:string limit:int32 = Chats;

//@description Returns a list of frequently used chats. Supported only if the chat info database is enabled @category Category of chats to be returned @limit Maximum number of chats to be returned; up to 30
getTopChats category:TopChatCategory limit:int32 = Chats;

//@description Removes a chat from the list of frequently used chats. Supported only if the chat info database is enabled @category Category of frequently used chats @chat_id Chat identifier
removeTopChat category:TopChatCategory chat_id:int53 = Ok;

//@description Adds a chat to the list of recently found chats. The chat is added to the beginning of the list. If the chat is already in the list, it will be removed from the list first @chat_id Identifier of the chat to add
addRecentlyFoundChat chat_id:int53 = Ok;

//@description Removes a chat from the list of recently found chats @chat_id Identifier of the chat to be removed
removeRecentlyFoundChat chat_id:int53 = Ok;

//@description Clears the list of recently found chats
clearRecentlyFoundChats = Ok;

//@description Checks whether a username can be set for a chat @chat_id Chat identifier; should be identifier of a supergroup chat, or a channel chat, or a private chat with self, or zero if chat is being created @username Username to be checked
checkChatUsername chat_id:int53 username:string = CheckChatUsernameResult;

//@description Returns a list of public chats created by the user
getCreatedPublicChats = Chats;


//@description Returns a list of common group chats with a given user. Chats are sorted by their type and creation date @user_id User identifier @offset_chat_id Chat identifier starting from which to return chats; use 0 for the first request @limit Maximum number of chats to be returned; up to 100
getGroupsInCommon user_id:int32 offset_chat_id:int53 limit:int32 = Chats;


//@description Returns messages in a chat. The messages are returned in a reverse chronological order (i.e., in order of decreasing message_id).
//-For optimal performance the number of returned messages is chosen by the library. This is an offline request if only_local is true
//@chat_id Chat identifier
//@from_message_id Identifier of the message starting from which history must be fetched; use 0 to get results from the last message
//@offset Specify 0 to get results from exactly the from_message_id or a negative offset up to 99 to get additionally some newer messages
//@limit The maximum number of messages to be returned; must be positive and can't be greater than 100. If the offset is negative, the limit must be greater or equal to -offset. Fewer messages may be returned than specified by the limit, even if the end of the message history has not been reached
//@only_local If true, returns only messages that are available locally without sending network requests
getChatHistory chat_id:int53 from_message_id:int53 offset:int32 limit:int32 only_local:Bool = Messages;

//@description Deletes all messages in the chat. Use Chat.can_be_deleted_only_for_self and Chat.can_be_deleted_for_all_users fields to find whether and how the method can be applied to the chat
//@chat_id Chat identifier @remove_from_chat_list Pass true if the chat should be removed from the chat list @revoke Pass true to try to delete chat history for all users
deleteChatHistory chat_id:int53 remove_from_chat_list:Bool revoke:Bool = Ok;

//@description Searches for messages with given words in the chat. Returns the results in reverse chronological order, i.e. in order of decreasing message_id. Cannot be used in secret chats with a non-empty query
//-(searchSecretMessages should be used instead), or without an enabled message database. For optimal performance the number of returned messages is chosen by the library
//@chat_id Identifier of the chat in which to search messages
//@query Query to search for
//@sender_user_id If not 0, only messages sent by the specified user will be returned. Not supported in secret chats
//@from_message_id Identifier of the message starting from which history must be fetched; use 0 to get results from the last message
//@offset Specify 0 to get results from exactly the from_message_id or a negative offset to get the specified message and some newer messages
//@limit The maximum number of messages to be returned; must be positive and can't be greater than 100. If the offset is negative, the limit must be greater than -offset. Fewer messages may be returned than specified by the limit, even if the end of the message history has not been reached
//@filter Filter for message content in the search results
searchChatMessages chat_id:int53 query:string sender_user_id:int32 from_message_id:int53 offset:int32 limit:int32 filter:SearchMessagesFilter = Messages;

//@description Searches for messages in all chats except secret chats. Returns the results in reverse chronological order (i.e., in order of decreasing (date, chat_id, message_id)).
//-For optimal performance the number of returned messages is chosen by the library
//@query Query to search for
//@offset_date The date of the message starting from which the results should be fetched. Use 0 or any date in the future to get results from the last message
//@offset_chat_id The chat identifier of the last found message, or 0 for the first request
//@offset_message_id The message identifier of the last found message, or 0 for the first request
//@limit The maximum number of messages to be returned, up to 100. Fewer messages may be returned than specified by the limit, even if the end of the message history has not been reached
searchMessages query:string offset_date:int32 offset_chat_id:int53 offset_message_id:int53 limit:int32 = Messages;

//@description Searches for messages in secret chats. Returns the results in reverse chronological order. For optimal performance the number of returned messages is chosen by the library
//@chat_id Identifier of the chat in which to search. Specify 0 to search in all secret chats @query Query to search for. If empty, searchChatMessages should be used instead
//@from_search_id The identifier from the result of a previous request, use 0 to get results from the last message
//@limit Maximum number of messages to be returned; up to 100. Fewer messages may be returned than specified by the limit, even if the end of the message history has not been reached
//@filter A filter for the content of messages in the search results
searchSecretMessages chat_id:int53 query:string from_search_id:int64 limit:int32 filter:SearchMessagesFilter = FoundMessages;

//@description Searches for call messages. Returns the results in reverse chronological order (i. e., in order of decreasing message_id). For optimal performance the number of returned messages is chosen by the library
//@from_message_id Identifier of the message from which to search; use 0 to get results from the last message
//@limit The maximum number of messages to be returned; up to 100. Fewer messages may be returned than specified by the limit, even if the end of the message history has not been reached @only_missed If true, returns only messages with missed calls
searchCallMessages from_message_id:int53 limit:int32 only_missed:Bool = Messages;

//@description Returns information about the recent locations of chat members that were sent to the chat. Returns up to 1 location message per user @chat_id Chat identifier @limit Maximum number of messages to be returned
searchChatRecentLocationMessages chat_id:int53 limit:int32 = Messages;

//@description Returns all active live locations that should be updated by the client. The list is persistent across application restarts only if the message database is used
getActiveLiveLocationMessages = Messages;

//@description Returns the last message sent in a chat no later than the specified date @chat_id Chat identifier @date Point in time (Unix timestamp) relative to which to search for messages
getChatMessageByDate chat_id:int53 date:int32 = Message;

//@description Returns approximate number of messages of the specified type in the chat @chat_id Identifier of the chat in which to count messages @filter Filter for message content; searchMessagesFilterEmpty is unsupported in this function @return_local If true, returns count that is available locally without sending network requests, returning -1 if the number of messages is unknown
getChatMessageCount chat_id:int53 filter:SearchMessagesFilter return_local:Bool = Count;


//@description Removes an active notification from notification list. Needs to be called only if the notification is removed by the current user @notification_group_id Identifier of notification group to which the notification belongs @notification_id Identifier of removed notification
removeNotification notification_group_id:int32 notification_id:int32 = Ok;

//@description Removes a group of active notifications. Needs to be called only if the notification group is removed by the current user @notification_group_id Notification group identifier @max_notification_id Maximum identifier of removed notifications
removeNotificationGroup notification_group_id:int32 max_notification_id:int32 = Ok;


//@description Returns a public HTTPS link to a message. Available only for messages in public supergroups and channels
//@chat_id Identifier of the chat to which the message belongs
//@message_id Identifier of the message
//@for_album Pass true if a link for a whole media album should be returned
getPublicMessageLink chat_id:int53 message_id:int53 for_album:Bool = PublicMessageLink;

//@description Returns a private HTTPS link to a message in a chat. Available only for already sent messages in supergroups and channels. The link will work only for members of the chat
//@chat_id Identifier of the chat to which the message belongs
//@message_id Identifier of the message
getMessageLink chat_id:int53 message_id:int53 = HttpUrl;


//@description Sends a message. Returns the sent message @chat_id Target chat @reply_to_message_id Identifier of the message to reply to or 0
//@disable_notification Pass true to disable notification for the message. Not supported in secret chats @from_background Pass true if the message is sent from the background
//@reply_markup Markup for replying to the message; for bots only @input_message_content The content of the message to be sent
sendMessage chat_id:int53 reply_to_message_id:int53 disable_notification:Bool from_background:Bool reply_markup:ReplyMarkup input_message_content:InputMessageContent = Message;

//@description Sends messages grouped together into an album. Currently only photo and video messages can be grouped into an album. Returns sent messages @chat_id Target chat @reply_to_message_id Identifier of a message to reply to or 0
//@disable_notification Pass true to disable notification for the messages. Not supported in secret chats @from_background Pass true if the messages are sent from the background
//@input_message_contents Contents of messages to be sent
sendMessageAlbum chat_id:int53 reply_to_message_id:int53 disable_notification:Bool from_background:Bool input_message_contents:vector<InputMessageContent> = Messages;

//@description Invites a bot to a chat (if it is not yet a member) and sends it the /start command. Bots can't be invited to a private chat other than the chat with the bot. Bots can't be invited to channels (although they can be added as admins) and secret chats. Returns the sent message
//@bot_user_id Identifier of the bot @chat_id Identifier of the target chat @parameter A hidden parameter sent to the bot for deep linking purposes (https://api.telegram.org/bots#deep-linking)
sendBotStartMessage bot_user_id:int32 chat_id:int53 parameter:string = Message;

//@description Sends the result of an inline query as a message. Returns the sent message. Always clears a chat draft message @chat_id Target chat @reply_to_message_id Identifier of a message to reply to or 0
//@disable_notification Pass true to disable notification for the message. Not supported in secret chats @from_background Pass true if the message is sent from background
//@query_id Identifier of the inline query @result_id Identifier of the inline result
//@hide_via_bot If true, there will be no mention of a bot, via which the message is sent. Can be used only for bots GetOption("animation_search_bot_username"), GetOption("photo_search_bot_username") and GetOption("venue_search_bot_username")
sendInlineQueryResultMessage chat_id:int53 reply_to_message_id:int53 disable_notification:Bool from_background:Bool query_id:int64 result_id:string hide_via_bot:Bool = Message;

//@description Forwards previously sent messages. Returns the forwarded messages in the same order as the message identifiers passed in message_ids. If a message can't be forwarded, null will be returned instead of the message
//@chat_id Identifier of the chat to which to forward messages @from_chat_id Identifier of the chat from which to forward messages @message_ids Identifiers of the messages to forward
//@disable_notification Pass true to disable notification for the message, doesn't work if messages are forwarded to a secret chat @from_background Pass true if the message is sent from the background
//@as_album True, if the messages should be grouped into an album after forwarding. For this to work, no more than 10 messages may be forwarded, and all of them must be photo or video messages
forwardMessages chat_id:int53 from_chat_id:int53 message_ids:vector<int53> disable_notification:Bool from_background:Bool as_album:Bool = Messages;

//@description Changes the current TTL setting (sets a new self-destruct timer) in a secret chat and sends the corresponding message @chat_id Chat identifier @ttl New TTL value, in seconds
sendChatSetTtlMessage chat_id:int53 ttl:int32 = Message;

//@description Sends a notification about a screenshot taken in a chat. Supported only in private and secret chats @chat_id Chat identifier
sendChatScreenshotTakenNotification chat_id:int53 = Ok;

//@description Adds a local message to a chat. The message is persistent across application restarts only if the message database is used. Returns the added message @chat_id Target chat @sender_user_id Identifier of the user who will be shown as the sender of the message; may be 0 for channel posts
//@reply_to_message_id Identifier of the message to reply to or 0 @disable_notification Pass true to disable notification for the message @input_message_content The content of the message to be added
addLocalMessage chat_id:int53 sender_user_id:int32 reply_to_message_id:int53 disable_notification:Bool input_message_content:InputMessageContent = Message;

//@description Deletes messages @chat_id Chat identifier @message_ids Identifiers of the messages to be deleted @revoke Pass true to try to delete messages for all chat members. Always true for supergroups, channels and secret chats
deleteMessages chat_id:int53 message_ids:vector<int53> revoke:Bool = Ok;

//@description Deletes all messages sent by the specified user to a chat. Supported only in supergroups; requires can_delete_messages administrator privileges @chat_id Chat identifier @user_id User identifier
deleteChatMessagesFromUser chat_id:int53 user_id:int32 = Ok;


//@description Edits the text of a message (or a text of a game message). Returns the edited message after the edit is completed on the server side
//@chat_id The chat the message belongs to @message_id Identifier of the message @reply_markup The new message reply markup; for bots only @input_message_content New text content of the message. Should be of type InputMessageText
editMessageText chat_id:int53 message_id:int53 reply_markup:ReplyMarkup input_message_content:InputMessageContent = Message;

//@description Edits the message content of a live location. Messages can be edited for a limited period of time specified in the live location. Returns the edited message after the edit is completed on the server side
//@chat_id The chat the message belongs to @message_id Identifier of the message @reply_markup The new message reply markup; for bots only @location New location content of the message; may be null. Pass null to stop sharing the live location
editMessageLiveLocation chat_id:int53 message_id:int53 reply_markup:ReplyMarkup location:location = Message;

//@description Edits the content of a message with an animation, an audio, a document, a photo or a video. The media in the message can't be replaced if the message was set to self-destruct. Media can't be replaced by self-destructing media. Media in an album can be edited only to contain a photo or a video. Returns the edited message after the edit is completed on the server side
//@chat_id The chat the message belongs to @message_id Identifier of the message @reply_markup The new message reply markup; for bots only @input_message_content New content of the message. Must be one of the following types: InputMessageAnimation, InputMessageAudio, InputMessageDocument, InputMessagePhoto or InputMessageVideo
editMessageMedia chat_id:int53 message_id:int53 reply_markup:ReplyMarkup input_message_content:InputMessageContent = Message;

//@description Edits the message content caption. Returns the edited message after the edit is completed on the server side
//@chat_id The chat the message belongs to @message_id Identifier of the message @reply_markup The new message reply markup; for bots only @caption New message content caption; 0-GetOption("message_caption_length_max") characters
editMessageCaption chat_id:int53 message_id:int53 reply_markup:ReplyMarkup caption:formattedText = Message;

//@description Edits the message reply markup; for bots only. Returns the edited message after the edit is completed on the server side
//@chat_id The chat the message belongs to @message_id Identifier of the message @reply_markup The new message reply markup
editMessageReplyMarkup chat_id:int53 message_id:int53 reply_markup:ReplyMarkup = Message;

//@description Edits the text of an inline text or game message sent via a bot; for bots only @inline_message_id Inline message identifier @reply_markup The new message reply markup @input_message_content New text content of the message. Should be of type InputMessageText
editInlineMessageText inline_message_id:string reply_markup:ReplyMarkup input_message_content:InputMessageContent = Ok;

//@description Edits the content of a live location in an inline message sent via a bot; for bots only @inline_message_id Inline message identifier @reply_markup The new message reply markup @location New location content of the message; may be null. Pass null to stop sharing the live location
editInlineMessageLiveLocation inline_message_id:string reply_markup:ReplyMarkup location:location = Ok;

//@description Edits the content of a message with an animation, an audio, a document, a photo or a video in an inline message sent via a bot; for bots only @inline_message_id Inline message identifier
//@reply_markup The new message reply markup; for bots only @input_message_content New content of the message. Must be one of the following types: InputMessageAnimation, InputMessageAudio, InputMessageDocument, InputMessagePhoto or InputMessageVideo
editInlineMessageMedia inline_message_id:string reply_markup:ReplyMarkup input_message_content:InputMessageContent = Ok;

//@description Edits the caption of an inline message sent via a bot; for bots only @inline_message_id Inline message identifier @reply_markup The new message reply markup @caption New message content caption; 0-GetOption("message_caption_length_max") characters
editInlineMessageCaption inline_message_id:string reply_markup:ReplyMarkup caption:formattedText = Ok;

//@description Edits the reply markup of an inline message sent via a bot; for bots only @inline_message_id Inline message identifier @reply_markup The new message reply markup
editInlineMessageReplyMarkup inline_message_id:string reply_markup:ReplyMarkup = Ok;


//@description Returns all entities (mentions, hashtags, cashtags, bot commands, URLs, and email addresses) contained in the text. This is an offline method. Can be called before authorization. Can be called synchronously @text The text in which to look for entites
getTextEntities text:string = TextEntities;

//@description Parses Bold, Italic, Code, Pre, PreCode and TextUrl entities contained in the text. This is an offline method. Can be called before authorization. Can be called synchronously @text The text which should be parsed @parse_mode Text parse mode
parseTextEntities text:string parse_mode:TextParseMode = FormattedText;

//@description Returns the MIME type of a file, guessed by its extension. Returns an empty string on failure. This is an offline method. Can be called before authorization. Can be called synchronously @file_name The name of the file or path to the file
getFileMimeType file_name:string = Text;

//@description Returns the extension of a file, guessed by its MIME type. Returns an empty string on failure. This is an offline method. Can be called before authorization. Can be called synchronously @mime_type The MIME type of the file
getFileExtension mime_type:string = Text;

//@description Removes potentially dangerous characters from the name of a file. The encoding of the file name is supposed to be UTF-8. Returns an empty string on failure. This is an offline method. Can be called before authorization. Can be called synchronously @file_name File name or path to the file
cleanFileName file_name:string = Text;

//@description Returns a string stored in the local database from the specified localization target and language pack by its key. Returns a 404 error if the string is not found. This is an offline method. Can be called before authorization. Can be called synchronously
//@language_pack_database_path Path to the language pack database in which strings are stored @localization_target Localization target to which the language pack belongs @language_pack_id Language pack identifier @key Language pack key of the string to be returned
getLanguagePackString language_pack_database_path:string localization_target:string language_pack_id:string key:string = LanguagePackStringValue;

//@description Converts a JSON-serialized string to corresponding JsonValue object. This is an offline method. Can be called before authorization. Can be called synchronously @json The JSON-serialized string
getJsonValue json:string = JsonValue;

//@description Converts a JsonValue object to corresponding JSON-serialized string. This is an offline method. Can be called before authorization. Can be called synchronously @json_value The JsonValue object
getJsonString json_value:JsonValue = Text;


//@description Changes user answer to a poll @chat_id Identifier of the chat to which the poll belongs @message_id Identifier of the message containing the poll
//@option_ids 0-based identifiers of options, chosen by the user. Currently user can't choose more than 1 option
setPollAnswer chat_id:int53 message_id:int53 option_ids:vector<int32> = Ok;

//@description Stops a poll. A poll in a message can be stopped when the message has can_be_edited flag set
//@chat_id Identifier of the chat to which the poll belongs @message_id Identifier of the message containing the poll @reply_markup The new message reply markup; for bots only
stopPoll chat_id:int53 message_id:int53 reply_markup:ReplyMarkup = Ok;


//@description Sends an inline query to a bot and returns its results. Returns an error with code 502 if the bot fails to answer the query before the query timeout expires @bot_user_id The identifier of the target bot
//@chat_id Identifier of the chat, where the query was sent @user_location Location of the user, only if needed @query Text of the query @offset Offset of the first entry to return
getInlineQueryResults bot_user_id:int32 chat_id:int53 user_location:location query:string offset:string = InlineQueryResults;

//@description Sets the result of an inline query; for bots only @inline_query_id Identifier of the inline query @is_personal True, if the result of the query can be cached for the specified user
//@results The results of the query @cache_time Allowed time to cache the results of the query, in seconds @next_offset Offset for the next inline query; pass an empty string if there are no more results
//@switch_pm_text If non-empty, this text should be shown on the button that opens a private chat with the bot and sends a start message to the bot with the parameter switch_pm_parameter @switch_pm_parameter The parameter for the bot start message
answerInlineQuery inline_query_id:int64 is_personal:Bool results:vector<InputInlineQueryResult> cache_time:int32 next_offset:string switch_pm_text:string switch_pm_parameter:string = Ok;


//@description Sends a callback query to a bot and returns an answer. Returns an error with code 502 if the bot fails to answer the query before the query timeout expires @chat_id Identifier of the chat with the message @message_id Identifier of the message from which the query originated @payload Query payload
getCallbackQueryAnswer chat_id:int53 message_id:int53 payload:CallbackQueryPayload = CallbackQueryAnswer;

//@description Sets the result of a callback query; for bots only @callback_query_id Identifier of the callback query @text Text of the answer @show_alert If true, an alert should be shown to the user instead of a toast notification @url URL to be opened @cache_time Time during which the result of the query can be cached, in seconds
answerCallbackQuery callback_query_id:int64 text:string show_alert:Bool url:string cache_time:int32 = Ok;


//@description Sets the result of a shipping query; for bots only @shipping_query_id Identifier of the shipping query @shipping_options Available shipping options @error_message An error message, empty on success
answerShippingQuery shipping_query_id:int64 shipping_options:vector<shippingOption> error_message:string = Ok;

//@description Sets the result of a pre-checkout query; for bots only @pre_checkout_query_id Identifier of the pre-checkout query @error_message An error message, empty on success
answerPreCheckoutQuery pre_checkout_query_id:int64 error_message:string = Ok;


//@description Updates the game score of the specified user in the game; for bots only @chat_id The chat to which the message with the game belongs @message_id Identifier of the message @edit_message True, if the message should be edited @user_id User identifier @score The new score
//@force Pass true to update the score even if it decreases. If the score is 0, the user will be deleted from the high score table
setGameScore chat_id:int53 message_id:int53 edit_message:Bool user_id:int32 score:int32 force:Bool = Message;

//@description Updates the game score of the specified user in a game; for bots only @inline_message_id Inline message identifier @edit_message True, if the message should be edited @user_id User identifier @score The new score
//@force Pass true to update the score even if it decreases. If the score is 0, the user will be deleted from the high score table
setInlineGameScore inline_message_id:string edit_message:Bool user_id:int32 score:int32 force:Bool = Ok;

//@description Returns the high scores for a game and some part of the high score table in the range of the specified user; for bots only @chat_id The chat that contains the message with the game @message_id Identifier of the message @user_id User identifier
getGameHighScores chat_id:int53 message_id:int53 user_id:int32 = GameHighScores;

//@description Returns game high scores and some part of the high score table in the range of the specified user; for bots only @inline_message_id Inline message identifier @user_id User identifier
getInlineGameHighScores inline_message_id:string user_id:int32 = GameHighScores;


//@description Deletes the default reply markup from a chat. Must be called after a one-time keyboard or a ForceReply reply markup has been used. UpdateChatReplyMarkup will be sent if the reply markup will be changed @chat_id Chat identifier
//@message_id The message identifier of the used keyboard
deleteChatReplyMarkup chat_id:int53 message_id:int53 = Ok;


//@description Sends a notification about user activity in a chat @chat_id Chat identifier @action The action description
sendChatAction chat_id:int53 action:ChatAction = Ok;


//@description Informs TDLib that the chat is opened by the user. Many useful activities depend on the chat being opened or closed (e.g., in supergroups and channels all updates are received only for opened chats) @chat_id Chat identifier
openChat chat_id:int53 = Ok;

//@description Informs TDLib that the chat is closed by the user. Many useful activities depend on the chat being opened or closed @chat_id Chat identifier
closeChat chat_id:int53 = Ok;

//@description Informs TDLib that messages are being viewed by the user. Many useful activities depend on whether the messages are currently being viewed or not (e.g., marking messages as read, incrementing a view counter, updating a view counter, removing deleted messages in supergroups and channels) @chat_id Chat identifier @message_ids The identifiers of the messages being viewed
//@force_read True, if messages in closed chats should be marked as read
viewMessages chat_id:int53 message_ids:vector<int53> force_read:Bool = Ok;

//@description Informs TDLib that the message content has been opened (e.g., the user has opened a photo, video, document, location or venue, or has listened to an audio file or voice note message). An updateMessageContentOpened update will be generated if something has changed @chat_id Chat identifier of the message @message_id Identifier of the message with the opened content
openMessageContent chat_id:int53 message_id:int53 = Ok;


//@description Marks all mentions in a chat as read @chat_id Chat identifier
readAllChatMentions chat_id:int53 = Ok;


//@description Returns an existing chat corresponding to a given user @user_id User identifier @force If true, the chat will be created without network request. In this case all information about the chat except its type, title and photo can be incorrect
createPrivateChat user_id:int32 force:Bool = Chat;

//@description Returns an existing chat corresponding to a known basic group @basic_group_id Basic group identifier @force If true, the chat will be created without network request. In this case all information about the chat except its type, title and photo can be incorrect
createBasicGroupChat basic_group_id:int32 force:Bool = Chat;

//@description Returns an existing chat corresponding to a known supergroup or channel @supergroup_id Supergroup or channel identifier @force If true, the chat will be created without network request. In this case all information about the chat except its type, title and photo can be incorrect
createSupergroupChat supergroup_id:int32 force:Bool = Chat;

//@description Returns an existing chat corresponding to a known secret chat @secret_chat_id Secret chat identifier
createSecretChat secret_chat_id:int32 = Chat;

//@description Creates a new basic group and sends a corresponding messageBasicGroupChatCreate. Returns the newly created chat @user_ids Identifiers of users to be added to the basic group @title Title of the new basic group; 1-128 characters
createNewBasicGroupChat user_ids:vector<int32> title:string = Chat;

//@description Creates a new supergroup or channel and sends a corresponding messageSupergroupChatCreate. Returns the newly created chat @title Title of the new chat; 1-128 characters @is_channel True, if a channel chat should be created @param_description Chat description; 0-255 characters
createNewSupergroupChat title:string is_channel:Bool description:string = Chat;

//@description Creates a new secret chat. Returns the newly created chat @user_id Identifier of the target user
createNewSecretChat user_id:int32 = Chat;

//@description Creates a new supergroup from an existing basic group and sends a corresponding messageChatUpgradeTo and messageChatUpgradeFrom. Deactivates the original basic group @chat_id Identifier of the chat to upgrade
upgradeBasicGroupChatToSupergroupChat chat_id:int53 = Chat;


//@description Changes the chat title. Supported only for basic groups, supergroups and channels. Requires administrator rights in basic groups and the appropriate administrator rights in supergroups and channels. The title will not be changed until the request to the server has been completed
//@chat_id Chat identifier @title New title of the chat; 1-128 characters
setChatTitle chat_id:int53 title:string = Ok;

//@description Changes the photo of a chat. Supported only for basic groups, supergroups and channels. Requires administrator rights in basic groups and the appropriate administrator rights in supergroups and channels. The photo will not be changed before request to the server has been completed
//@chat_id Chat identifier @photo New chat photo. You can use a zero InputFileId to delete the chat photo. Files that are accessible only by HTTP URL are not acceptable
setChatPhoto chat_id:int53 photo:InputFile = Ok;

//@description Changes the draft message in a chat @chat_id Chat identifier @draft_message New draft message; may be null
setChatDraftMessage chat_id:int53 draft_message:draftMessage = Ok;

//@description Changes the notification settings of a chat @chat_id Chat identifier @notification_settings New notification settings for the chat
setChatNotificationSettings chat_id:int53 notification_settings:chatNotificationSettings = Ok;

//@description Changes the pinned state of a chat. You can pin up to GetOption("pinned_chat_count_max") non-secret chats and the same number of secret chats @chat_id Chat identifier @is_pinned New value of is_pinned
toggleChatIsPinned chat_id:int53 is_pinned:Bool = Ok;

//@description Changes the marked as unread state of a chat @chat_id Chat identifier @is_marked_as_unread New value of is_marked_as_unread
toggleChatIsMarkedAsUnread chat_id:int53 is_marked_as_unread:Bool = Ok;

//@description Changes the value of the default disable_notification parameter, used when a message is sent to a chat @chat_id Chat identifier @default_disable_notification New value of default_disable_notification
toggleChatDefaultDisableNotification chat_id:int53 default_disable_notification:Bool = Ok;

//@description Changes client data associated with a chat @chat_id Chat identifier @client_data New value of client_data
setChatClientData chat_id:int53 client_data:string = Ok;

//@description Pins a message in a chat; requires appropriate administrator rights in the group or channel @chat_id Identifier of the chat @message_id Identifier of the new pinned message @disable_notification True, if there should be no notification about the pinned message
pinChatMessage chat_id:int53 message_id:int53 disable_notification:Bool = Ok;

//@description Removes the pinned message from a chat; requires appropriate administrator rights in the group or channel @chat_id Identifier of the chat
unpinChatMessage chat_id:int53 = Ok;

//@description Adds current user as a new member to a chat. Private and secret chats can't be joined using this method @chat_id Chat identifier
joinChat chat_id:int53 = Ok;

//@description Removes current user from chat members. Private and secret chats can't be left using this method @chat_id Chat identifier
leaveChat chat_id:int53 = Ok;

//@description Adds a new member to a chat. Members can't be added to private or secret chats. Members will not be added until the chat state has been synchronized with the server
//@chat_id Chat identifier @user_id Identifier of the user @forward_limit The number of earlier messages from the chat to be forwarded to the new member; up to 100. Ignored for supergroups and channels
addChatMember chat_id:int53 user_id:int32 forward_limit:int32 = Ok;

//@description Adds multiple new members to a chat. Currently this option is only available for supergroups and channels. This option can't be used to join a chat. Members can't be added to a channel if it has more than 200 members. Members will not be added until the chat state has been synchronized with the server
//@chat_id Chat identifier @user_ids Identifiers of the users to be added to the chat
addChatMembers chat_id:int53 user_ids:vector<int32> = Ok;

//@description Changes the status of a chat member, needs appropriate privileges. This function is currently not suitable for adding new members to the chat; instead, use addChatMember. The chat member status will not be changed until it has been synchronized with the server
//@chat_id Chat identifier @user_id User identifier @status The new status of the member in the chat
setChatMemberStatus chat_id:int53 user_id:int32 status:ChatMemberStatus = Ok;

//@description Returns information about a single member of a chat @chat_id Chat identifier @user_id User identifier
getChatMember chat_id:int53 user_id:int32 = ChatMember;

//@description Searches for a specified query in the first name, last name and username of the members of a specified chat. Requires administrator rights in channels @chat_id Chat identifier @query Query to search for @limit The maximum number of users to be returned @filter The type of users to return. By default, chatMembersFilterMembers
searchChatMembers chat_id:int53 query:string limit:int32 filter:ChatMembersFilter = ChatMembers;

//@description Returns a list of users who are administrators of the chat @chat_id Chat identifier
getChatAdministrators chat_id:int53 = Users;


//@description Clears draft messages in all chats @exclude_secret_chats If true, local draft messages in secret chats will not be cleared
clearAllDraftMessages exclude_secret_chats:Bool = Ok;


//@description Returns list of chats with non-default notification settings @scope If specified, only chats from the specified scope will be returned @compare_sound If true, also chats with non-default sound will be returned
getChatNotificationSettingsExceptions scope:NotificationSettingsScope compare_sound:Bool = Chats;

//@description Returns the notification settings for chats of a given type @scope Types of chats for which to return the notification settings information
getScopeNotificationSettings scope:NotificationSettingsScope = ScopeNotificationSettings;

//@description Changes notification settings for chats of a given type @scope Types of chats for which to change the notification settings @notification_settings The new notification settings for the given scope
setScopeNotificationSettings scope:NotificationSettingsScope notification_settings:scopeNotificationSettings = Ok;

//@description Resets all notification settings to their default values. By default, all chats are unmuted, the sound is set to "default" and message previews are shown
resetAllNotificationSettings = Ok;


//@description Changes the order of pinned chats @chat_ids The new list of pinned chats
setPinnedChats chat_ids:vector<int53> = Ok;


//@description Downloads a file from the cloud. Download progress and completion of the download will be notified through updateFile updates
//@file_id Identifier of the file to download
//@priority Priority of the download (1-32). The higher the priority, the earlier the file will be downloaded. If the priorities of two files are equal, then the last one for which downloadFile was called will be downloaded first
//@offset The starting position from which the file should be downloaded
//@limit Number of bytes which should be downloaded starting from the "offset" position before the download will be automatically cancelled; use 0 to download without a limit
//@synchronous If false, this request returns file state just after the download has been started. If true, this request returns file state only after
//-the download has succeeded, has failed, has been cancelled or a new downloadFile request with different offset/limit parameters was sent
downloadFile file_id:int32 priority:int32 offset:int32 limit:int32 synchronous:Bool = File;

//@description Returns file downloaded prefix size from a given offset @file_id Identifier of the file @offset Offset from which downloaded prefix size should be calculated
getFileDownloadedPrefixSize file_id:int32 offset:int32 = Count;

//@description Stops the downloading of a file. If a file has already been downloaded, does nothing @file_id Identifier of a file to stop downloading @only_if_pending Pass true to stop downloading only if it hasn't been started, i.e. request hasn't been sent to server
cancelDownloadFile file_id:int32 only_if_pending:Bool = Ok;

//@description Asynchronously uploads a file to the cloud without sending it in a message. updateFile will be used to notify about upload progress and successful completion of the upload. The file will not have a persistent remote identifier until it will be sent in a message @file File to upload @file_type File type
//@priority Priority of the upload (1-32). The higher the priority, the earlier the file will be uploaded. If the priorities of two files are equal, then the first one for which uploadFile was called will be uploaded first
uploadFile file:InputFile file_type:FileType priority:int32 = File;

//@description Stops the uploading of a file. Supported only for files uploaded by using uploadFile. For other files the behavior is undefined @file_id Identifier of the file to stop uploading
cancelUploadFile file_id:int32 = Ok;

//@description Writes a part of a generated file. This method is intended to be used only if the client has no direct access to TDLib's file system, because it is usually slower than a direct write to the destination file
//@generation_id The identifier of the generation process @offset The offset from which to write the data to the file @data The data to write
writeGeneratedFilePart generation_id:int64 offset:int32 data:bytes = Ok;

//@description Informs TDLib on a file generation prograss
//@generation_id The identifier of the generation process
//@expected_size Expected size of the generated file, in bytes; 0 if unknown
//@local_prefix_size The number of bytes already generated
setFileGenerationProgress generation_id:int64 expected_size:int32 local_prefix_size:int32 = Ok;

//@description Finishes the file generation
//@generation_id The identifier of the generation process
//@error If set, means that file generation has failed and should be terminated
finishFileGeneration generation_id:int64 error:error = Ok;

//@description Reads a part of a file from the TDLib file cache and returns read bytes. This method is intended to be used only if the client has no direct access to TDLib's file system, because it is usually slower than a direct read from the file
//@file_id Identifier of the file. The file must be located in the TDLib file cache
//@offset The offset from which to read the file
//@count Number of bytes to read. An error will be returned if there are not enough bytes available in the file from the specified position. Pass 0 to read all available data from the specified position
readFilePart file_id:int32 offset:int32 count:int32 = FilePart;

//@description Deletes a file from the TDLib file cache @file_id Identifier of the file to delete
deleteFile file_id:int32 = Ok;


//@description Generates a new invite link for a chat; the previously generated link is revoked. Available for basic groups, supergroups, and channels. In basic groups this can be called only by the group's creator; in supergroups and channels this requires appropriate administrator rights @chat_id Chat identifier
generateChatInviteLink chat_id:int53 = ChatInviteLink;

//@description Checks the validity of an invite link for a chat and returns information about the corresponding chat @invite_link Invite link to be checked; should begin with "https://t.me/joinchat/", "https://telegram.me/joinchat/", or "https://telegram.dog/joinchat/"
checkChatInviteLink invite_link:string = ChatInviteLinkInfo;

//@description Uses an invite link to add the current user to the chat if possible. The new member will not be added until the chat state has been synchronized with the server
//@invite_link Invite link to import; should begin with "https://t.me/joinchat/", "https://telegram.me/joinchat/", or "https://telegram.dog/joinchat/"
joinChatByInviteLink invite_link:string = Chat;


//@description Creates a new call @user_id Identifier of the user to be called @protocol Description of the call protocols supported by the client
createCall user_id:int32 protocol:callProtocol = CallId;

//@description Accepts an incoming call @call_id Call identifier @protocol Description of the call protocols supported by the client
acceptCall call_id:int32 protocol:callProtocol = Ok;

//@description Discards a call @call_id Call identifier @is_disconnected True, if the user was disconnected @duration The call duration, in seconds @connection_id Identifier of the connection used during the call
discardCall call_id:int32 is_disconnected:Bool duration:int32 connection_id:int64 = Ok;

//@description Sends a call rating @call_id Call identifier @rating Call rating; 1-5 @comment An optional user comment if the rating is less than 5
sendCallRating call_id:int32 rating:int32 comment:string = Ok;

//@description Sends debug information for a call @call_id Call identifier @debug_information Debug information in application-specific format
sendCallDebugInformation call_id:int32 debug_information:string = Ok;



//@description Adds a user to the blacklist @user_id User identifier
blockUser user_id:int32 = Ok;

//@description Removes a user from the blacklist @user_id User identifier
unblockUser user_id:int32 = Ok;

//@description Returns users that were blocked by the current user @offset Number of users to skip in the result; must be non-negative @limit Maximum number of users to return; up to 100
getBlockedUsers offset:int32 limit:int32 = Users;


//@description Adds new contacts or edits existing contacts; contacts' user identifiers are ignored @contacts The list of contacts to import or edit, contact's vCard are ignored and are not imported
importContacts contacts:vector<contact> = ImportedContacts;

//@description Returns all user contacts
getContacts = Users;

//@description Searches for the specified query in the first names, last names and usernames of the known user contacts @query Query to search for; may be empty to return all contacts @limit Maximum number of users to be returned
searchContacts query:string limit:int32 = Users;

//@description Removes users from the contact list @user_ids Identifiers of users to be deleted
removeContacts user_ids:vector<int32> = Ok;

//@description Returns the total number of imported contacts
getImportedContactCount = Count;

//@description Changes imported contacts using the list of current user contacts saved on the device. Imports newly added contacts and, if at least the file database is enabled, deletes recently deleted contacts.
//-Query result depends on the result of the previous query, so only one query is possible at the same time @contacts The new list of contacts, contact's vCard are ignored and are not imported
changeImportedContacts contacts:vector<contact> = ImportedContacts;

//@description Clears all imported contacts, contact list remains unchanged
clearImportedContacts = Ok;


//@description Returns the profile photos of a user. The result of this query may be outdated: some photos might have been deleted already @user_id User identifier @offset The number of photos to skip; must be non-negative @limit Maximum number of photos to be returned; up to 100
getUserProfilePhotos user_id:int32 offset:int32 limit:int32 = UserProfilePhotos;


//@description Returns stickers from the installed sticker sets that correspond to a given emoji. If the emoji is not empty, favorite and recently used stickers may also be returned @emoji String representation of emoji. If empty, returns all known installed stickers @limit Maximum number of stickers to be returned
getStickers emoji:string limit:int32 = Stickers;

//@description Searches for stickers from public sticker sets that correspond to a given emoji @emoji String representation of emoji; must be non-empty @limit Maximum number of stickers to be returned
searchStickers emoji:string limit:int32 = Stickers;

//@description Returns a list of installed sticker sets @is_masks Pass true to return mask sticker sets; pass false to return ordinary sticker sets
getInstalledStickerSets is_masks:Bool = StickerSets;

//@description Returns a list of archived sticker sets @is_masks Pass true to return mask stickers sets; pass false to return ordinary sticker sets @offset_sticker_set_id Identifier of the sticker set from which to return the result @limit Maximum number of sticker sets to return
getArchivedStickerSets is_masks:Bool offset_sticker_set_id:int64 limit:int32 = StickerSets;

//@description Returns a list of trending sticker sets
getTrendingStickerSets = StickerSets;

//@description Returns a list of sticker sets attached to a file. Currently only photos and videos can have attached sticker sets @file_id File identifier
getAttachedStickerSets file_id:int32 = StickerSets;

//@description Returns information about a sticker set by its identifier @set_id Identifier of the sticker set
getStickerSet set_id:int64 = StickerSet;

//@description Searches for a sticker set by its name @name Name of the sticker set
searchStickerSet name:string = StickerSet;

//@description Searches for installed sticker sets by looking for specified query in their title and name @is_masks Pass true to return mask sticker sets; pass false to return ordinary sticker sets @query Query to search for @limit Maximum number of sticker sets to return
searchInstalledStickerSets is_masks:Bool query:string limit:int32 = StickerSets;

//@description Searches for ordinary sticker sets by looking for specified query in their title and name. Excludes installed sticker sets from the results @query Query to search for
searchStickerSets query:string = StickerSets;

//@description Installs/uninstalls or activates/archives a sticker set @set_id Identifier of the sticker set @is_installed The new value of is_installed @is_archived The new value of is_archived. A sticker set can't be installed and archived simultaneously
changeStickerSet set_id:int64 is_installed:Bool is_archived:Bool = Ok;

//@description Informs the server that some trending sticker sets have been viewed by the user @sticker_set_ids Identifiers of viewed trending sticker sets
viewTrendingStickerSets sticker_set_ids:vector<int64> = Ok;

//@description Changes the order of installed sticker sets @is_masks Pass true to change the order of mask sticker sets; pass false to change the order of ordinary sticker sets @sticker_set_ids Identifiers of installed sticker sets in the new correct order
reorderInstalledStickerSets is_masks:Bool sticker_set_ids:vector<int64> = Ok;

//@description Returns a list of recently used stickers @is_attached Pass true to return stickers and masks that were recently attached to photos or video files; pass false to return recently sent stickers
getRecentStickers is_attached:Bool = Stickers;

//@description Manually adds a new sticker to the list of recently used stickers. The new sticker is added to the top of the list. If the sticker was already in the list, it is removed from the list first. Only stickers belonging to a sticker set can be added to this list
//@is_attached Pass true to add the sticker to the list of stickers recently attached to photo or video files; pass false to add the sticker to the list of recently sent stickers @sticker Sticker file to add
addRecentSticker is_attached:Bool sticker:InputFile = Stickers;

//@description Removes a sticker from the list of recently used stickers @is_attached Pass true to remove the sticker from the list of stickers recently attached to photo or video files; pass false to remove the sticker from the list of recently sent stickers @sticker Sticker file to delete
removeRecentSticker is_attached:Bool sticker:InputFile = Ok;

//@description Clears the list of recently used stickers @is_attached Pass true to clear the list of stickers recently attached to photo or video files; pass false to clear the list of recently sent stickers
clearRecentStickers is_attached:Bool = Ok;

//@description Returns favorite stickers
getFavoriteStickers = Stickers;

//@description Adds a new sticker to the list of favorite stickers. The new sticker is added to the top of the list. If the sticker was already in the list, it is removed from the list first. Only stickers belonging to a sticker set can be added to this list
//@sticker Sticker file to add
addFavoriteSticker sticker:InputFile = Ok;

//@description Removes a sticker from the list of favorite stickers @sticker Sticker file to delete from the list
removeFavoriteSticker sticker:InputFile = Ok;

//@description Returns emoji corresponding to a sticker @sticker Sticker file identifier
getStickerEmojis sticker:InputFile = StickerEmojis;


//@description Returns saved animations
getSavedAnimations = Animations;

//@description Manually adds a new animation to the list of saved animations. The new animation is added to the beginning of the list. If the animation was already in the list, it is removed first. Only non-secret video animations with MIME type "video/mp4" can be added to the list
//@animation The animation file to be added. Only animations known to the server (i.e. successfully sent via a message) can be added to the list
addSavedAnimation animation:InputFile = Ok;

//@description Removes an animation from the list of saved animations @animation Animation file to be removed
removeSavedAnimation animation:InputFile = Ok;


//@description Returns up to 20 recently used inline bots in the order of their last usage
getRecentInlineBots = Users;


//@description Searches for recently used hashtags by their prefix @prefix Hashtag prefix to search for @limit Maximum number of hashtags to be returned
searchHashtags prefix:string limit:int32 = Hashtags;

//@description Removes a hashtag from the list of recently used hashtags @hashtag Hashtag to delete
removeRecentHashtag hashtag:string = Ok;


//@description Returns a web page preview by the text of the message. Do not call this function too often. Returns a 404 error if the web page has no preview @text Message text with formatting
getWebPagePreview text:formattedText = WebPage;

//@description Returns an instant view version of a web page if available. Returns a 404 error if the web page has no instant view page @url The web page URL @force_full If true, the full instant view for the web page will be returned
getWebPageInstantView url:string force_full:Bool = WebPageInstantView;


//@description Uploads a new profile photo for the current user. If something changes, updateUser will be sent @photo Profile photo to set. inputFileId and inputFileRemote may still be unsupported
setProfilePhoto photo:InputFile = Ok;

//@description Deletes a profile photo. If something changes, updateUser will be sent @profile_photo_id Identifier of the profile photo to delete
deleteProfilePhoto profile_photo_id:int64 = Ok;

//@description Changes the first and last name of the current user. If something changes, updateUser will be sent @first_name The new value of the first name for the user; 1-64 characters @last_name The new value of the optional last name for the user; 0-64 characters
setName first_name:string last_name:string = Ok;

//@description Changes the bio of the current user @bio The new value of the user bio; 0-70 characters without line feeds
setBio bio:string = Ok;

//@description Changes the username of the current user. If something changes, updateUser will be sent @username The new value of the username. Use an empty string to remove the username
setUsername username:string = Ok;

//@description Changes the phone number of the user and sends an authentication code to the user's new phone number. On success, returns information about the sent code
//@phone_number The new phone number of the user in international format @allow_flash_call Pass true if the code can be sent via flash call to the specified phone number @is_current_phone_number Pass true if the phone number is used on the current device. Ignored if allow_flash_call is false
changePhoneNumber phone_number:string allow_flash_call:Bool is_current_phone_number:Bool = AuthenticationCodeInfo;

//@description Re-sends the authentication code sent to confirm a new phone number for the user. Works only if the previously received authenticationCodeInfo next_code_type was not null
resendChangePhoneNumberCode = AuthenticationCodeInfo;

//@description Checks the authentication code sent to confirm a new phone number of the user @code Verification code received by SMS, phone call or flash call
checkChangePhoneNumberCode code:string = Ok;


//@description Returns all active sessions of the current user
getActiveSessions = Sessions;

//@description Terminates a session of the current user @session_id Session identifier
terminateSession session_id:int64 = Ok;

//@description Terminates all other sessions of the current user
terminateAllOtherSessions = Ok;


//@description Returns all website where the current user used Telegram to log in
getConnectedWebsites = ConnectedWebsites;

//@description Disconnects website from the current user's Telegram account @website_id Website identifier
disconnectWebsite website_id:int64 = Ok;

//@description Disconnects all websites from the current user's Telegram account
disconnectAllWebsites = Ok;


//@description Toggles the "All members are admins" setting in basic groups; requires creator privileges in the group @basic_group_id Identifier of the basic group @everyone_is_administrator New value of everyone_is_administrator
toggleBasicGroupAdministrators basic_group_id:int32 everyone_is_administrator:Bool = Ok;


//@description Changes the username of a supergroup or channel, requires creator privileges in the supergroup or channel @supergroup_id Identifier of the supergroup or channel @username New value of the username. Use an empty string to remove the username
setSupergroupUsername supergroup_id:int32 username:string = Ok;

//@description Changes the sticker set of a supergroup; requires appropriate rights in the supergroup @supergroup_id Identifier of the supergroup @sticker_set_id New value of the supergroup sticker set identifier. Use 0 to remove the supergroup sticker set
setSupergroupStickerSet supergroup_id:int32 sticker_set_id:int64 = Ok;

//@description Toggles whether all members of a supergroup can add new members; requires appropriate administrator rights in the supergroup. @supergroup_id Identifier of the supergroup @anyone_can_invite New value of anyone_can_invite
toggleSupergroupInvites supergroup_id:int32 anyone_can_invite:Bool = Ok;

//@description Toggles sender signatures messages sent in a channel; requires appropriate administrator rights in the channel. @supergroup_id Identifier of the channel @sign_messages New value of sign_messages
toggleSupergroupSignMessages supergroup_id:int32 sign_messages:Bool = Ok;

//@description Toggles whether the message history of a supergroup is available to new members; requires appropriate administrator rights in the supergroup. @supergroup_id The identifier of the supergroup @is_all_history_available The new value of is_all_history_available
toggleSupergroupIsAllHistoryAvailable supergroup_id:int32 is_all_history_available:Bool = Ok;

//@description Changes information about a supergroup or channel; requires appropriate administrator rights @supergroup_id Identifier of the supergroup or channel @param_description New supergroup or channel description; 0-255 characters
setSupergroupDescription supergroup_id:int32 description:string = Ok;

//@description Reports some messages from a user in a supergroup as spam; requires administrator rights in the supergroup @supergroup_id Supergroup identifier @user_id User identifier @message_ids Identifiers of messages sent in the supergroup by the user. This list must be non-empty
reportSupergroupSpam supergroup_id:int32 user_id:int32 message_ids:vector<int53> = Ok;

//@description Returns information about members or banned users in a supergroup or channel. Can be used only if SupergroupFullInfo.can_get_members == true; additionally, administrator privileges may be required for some filters @supergroup_id Identifier of the supergroup or channel
//@filter The type of users to return. By default, supergroupMembersRecent @offset Number of users to skip @limit The maximum number of users be returned; up to 200
getSupergroupMembers supergroup_id:int32 filter:SupergroupMembersFilter offset:int32 limit:int32 = ChatMembers;

//@description Deletes a supergroup or channel along with all messages in the corresponding chat. This will release the supergroup or channel username and remove all members; requires creator privileges in the supergroup or channel. Chats with more than 1000 members can't be deleted using this method @supergroup_id Identifier of the supergroup or channel
deleteSupergroup supergroup_id:int32 = Ok;


//@description Closes a secret chat, effectively transfering its state to secretChatStateClosed @secret_chat_id Secret chat identifier
closeSecretChat secret_chat_id:int32 = Ok;


//@description Returns a list of service actions taken by chat members and administrators in the last 48 hours. Available only in supergroups and channels. Requires administrator rights. Returns results in reverse chronological order (i. e., in order of decreasing event_id)
//@chat_id Chat identifier @query Search query by which to filter events @from_event_id Identifier of an event from which to return results. Use 0 to get results from the latest events @limit Maximum number of events to return; up to 100
//@filters The types of events to return. By default, all types will be returned @user_ids User identifiers by which to filter events. By default, events relating to all users will be returned
getChatEventLog chat_id:int53 query:string from_event_id:int64 limit:int32 filters:chatEventLogFilters user_ids:vector<int32> = ChatEvents;


//@description Returns an invoice payment form. This method should be called when the user presses inlineKeyboardButtonBuy @chat_id Chat identifier of the Invoice message @message_id Message identifier
getPaymentForm chat_id:int53 message_id:int53 = PaymentForm;

//@description Validates the order information provided by a user and returns the available shipping options for a flexible invoice @chat_id Chat identifier of the Invoice message @message_id Message identifier @order_info The order information, provided by the user @allow_save True, if the order information can be saved
validateOrderInfo chat_id:int53 message_id:int53 order_info:orderInfo allow_save:Bool = ValidatedOrderInfo;

//@description Sends a filled-out payment form to the bot for final verification @chat_id Chat identifier of the Invoice message @message_id Message identifier @order_info_id Identifier returned by ValidateOrderInfo, or an empty string @shipping_option_id Identifier of a chosen shipping option, if applicable
//@credentials The credentials chosen by user for payment
sendPaymentForm chat_id:int53 message_id:int53 order_info_id:string shipping_option_id:string credentials:InputCredentials = PaymentResult;

//@description Returns information about a successful payment @chat_id Chat identifier of the PaymentSuccessful message @message_id Message identifier
getPaymentReceipt chat_id:int53 message_id:int53 = PaymentReceipt;

//@description Returns saved order info, if any
getSavedOrderInfo = OrderInfo;

//@description Deletes saved order info
deleteSavedOrderInfo = Ok;

//@description Deletes saved credentials for all payment provider bots
deleteSavedCredentials = Ok;


//@description Returns a user that can be contacted to get support
getSupportUser = User;

//@description Returns background wallpapers
getWallpapers = Wallpapers;


//@description Returns information about the current localization target. This is an offline request if only_local is true. Can be called before authorization @only_local If true, returns only locally available information without sending network requests
getLocalizationTargetInfo only_local:Bool = LocalizationTargetInfo;

//@description Returns information about a language pack. Returned language pack identifier may be different from a provided one. Can be called before authorization @language_pack_id Language pack identifier
getLanguagePackInfo language_pack_id:string = LanguagePackInfo;

//@description Returns strings from a language pack in the current localization target by their keys. Can be called before authorization @language_pack_id Language pack identifier of the strings to be returned @keys Language pack keys of the strings to be returned; leave empty to request all available strings
getLanguagePackStrings language_pack_id:string keys:vector<string> = LanguagePackStrings;

//@description Fetches the latest versions of all strings from a language pack in the current localization target from the server. This method doesn't need to be called explicitly for the current used/base language packs. Can be called before authorization @language_pack_id Language pack identifier
synchronizeLanguagePack language_pack_id:string = Ok;

//@description Adds a custom server language pack to the list of installed language packs in current localization target. Can be called before authorization @language_pack_id Identifier of a language pack to be added; may be different from a name that is used in an "https://t.me/setlanguage/" link
addCustomServerLanguagePack language_pack_id:string = Ok;

//@description Adds or changes a custom local language pack to the current localization target @info Information about the language pack. Language pack ID must start with 'X', consist only of English letters, digits and hyphens, and must not exceed 64 characters. Can be called before authorization @strings Strings of the new language pack
setCustomLanguagePack info:languagePackInfo strings:vector<languagePackString> = Ok;

//@description Edits information about a custom local language pack in the current localization target. Can be called before authorization @info New information about the custom local language pack
editCustomLanguagePackInfo info:languagePackInfo = Ok;

//@description Adds, edits or deletes a string in a custom local language pack. Can be called before authorization @language_pack_id Identifier of a previously added custom local language pack in the current localization target @new_string New language pack string
setCustomLanguagePackString language_pack_id:string new_string:languagePackString = Ok;

//@description Deletes all information about a language pack in the current localization target. The language pack which is currently in use (including base language pack) or is being synchronized can't be deleted. Can be called before authorization @language_pack_id Identifier of the language pack to delete
deleteLanguagePack language_pack_id:string = Ok;


//@description Registers the currently used device for receiving push notifications. Returns a globally unique identifier of the push notification subscription @device_token Device token @other_user_ids List of user identifiers of other users currently using the client
registerDevice device_token:DeviceToken other_user_ids:vector<int32> = PushReceiverId;

//@description Handles a push notification. Returns error with code 406 if the push notification is not supported and connection to the server is required to fetch new data. Can be called before authorization
//@payload JSON-encoded push notification payload with all fields sent by the server, and "google.sent_time" and "google.notification.sound" fields added
processPushNotification payload:string = Ok;

//@description Returns a globally unique push notification subscription identifier for identification of an account, which has received a push notification. This is an offline method. Can be called before authorization. Can be called synchronously @payload JSON-encoded push notification payload
getPushReceiverId payload:string = PushReceiverId;


//@description Returns t.me URLs recently visited by a newly registered user @referrer Google Play referrer to identify the user
getRecentlyVisitedTMeUrls referrer:string = TMeUrls;


//@description Changes user privacy settings @setting The privacy setting @rules The new privacy rules
setUserPrivacySettingRules setting:UserPrivacySetting rules:userPrivacySettingRules = Ok;

//@description Returns the current privacy settings @setting The privacy setting
getUserPrivacySettingRules setting:UserPrivacySetting = UserPrivacySettingRules;


//@description Returns the value of an option by its name. (Check the list of available options on https://core.telegram.org/tdlib/options.) Can be called before authorization
//@name The name of the option
getOption name:string = OptionValue;

//@description Sets the value of an option. (Check the list of available options on https://core.telegram.org/tdlib/options.) Only writable options can be set. Can be called before authorization
//@name The name of the option @value The new value of the option
setOption name:string value:OptionValue = Ok;


//@description Changes the period of inactivity after which the account of the current user will automatically be deleted @ttl New account TTL
setAccountTtl ttl:accountTtl = Ok;

//@description Returns the period of inactivity after which the account of the current user will automatically be deleted
getAccountTtl = AccountTtl;

//@description Deletes the account of the current user, deleting all information associated with the user from the server. The phone number of the account can be used to create a new account. Can be called before authorization when the current authorization state is authorizationStateWaitPassword @reason The reason why the account was deleted; optional
deleteAccount reason:string = Ok;


//@description Returns information on whether the current chat can be reported as spam @chat_id Chat identifier
getChatReportSpamState chat_id:int53 = ChatReportSpamState;

//@description Reports to the server whether a chat is a spam chat or not. Can be used only if ChatReportSpamState.can_report_spam is true. After this request, ChatReportSpamState.can_report_spam becomes false forever @chat_id Chat identifier @is_spam_chat If true, the chat will be reported as spam; otherwise it will be marked as not spam
changeChatReportSpamState chat_id:int53 is_spam_chat:Bool = Ok;

//@description Reports a chat to the Telegram moderators. Supported only for supergroups, channels, or private chats with bots, since other chats can't be checked by moderators @chat_id Chat identifier @reason The reason for reporting the chat @message_ids Identifiers of reported messages, if any
reportChat chat_id:int53 reason:ChatReportReason message_ids:vector<int53> = Ok;


//@description Returns URL with the chat statistics. Currently this method can be used only for channels @chat_id Chat identifier @parameters Parameters from "tg://statsrefresh?params=******" link @is_dark Pass true if a URL with the dark theme must be returned
getChatStatisticsUrl chat_id:int53 parameters:string is_dark:Bool = HttpUrl;


//@description Returns storage usage statistics. Can be called before authorization @chat_limit Maximum number of chats with the largest storage usage for which separate statistics should be returned. All other chats will be grouped in entries with chat_id == 0. If the chat info database is not used, the chat_limit is ignored and is always set to 0
getStorageStatistics chat_limit:int32 = StorageStatistics;

//@description Quickly returns approximate storage usage statistics. Can be called before authorization
getStorageStatisticsFast = StorageStatisticsFast;

//@description Returns database statistics
getDatabaseStatistics = DatabaseStatistics;

//@description Optimizes storage usage, i.e. deletes some files and returns new storage usage statistics. Secret thumbnails can't be deleted
//@size Limit on the total size of files after deletion. Pass -1 to use the default limit
//@ttl Limit on the time that has passed since the last time a file was accessed (or creation time for some filesystems). Pass -1 to use the default limit
//@count Limit on the total count of files after deletion. Pass -1 to use the default limit
//@immunity_delay The amount of time after the creation of a file during which it can't be deleted, in seconds. Pass -1 to use the default value
//@file_types If not empty, only files with the given type(s) are considered. By default, all types except thumbnails, profile photos, stickers and wallpapers are deleted
//@chat_ids If not empty, only files from the given chats are considered. Use 0 as chat identifier to delete files not belonging to any chat (e.g., profile photos)
//@exclude_chat_ids If not empty, files from the given chats are excluded. Use 0 as chat identifier to exclude all files not belonging to any chat (e.g., profile photos)
//@chat_limit Same as in getStorageStatistics. Affects only returned statistics
optimizeStorage size:int53 ttl:int32 count:int32 immunity_delay:int32 file_types:vector<FileType> chat_ids:vector<int53> exclude_chat_ids:vector<int53> chat_limit:int32 = StorageStatistics;


//@description Sets the current network type. Can be called before authorization. Calling this method forces all network connections to reopen, mitigating the delay in switching between different networks, so it should be called whenever the network is changed, even if the network type remains the same.
//-Network type is used to check whether the library can use the network at all and also for collecting detailed network data usage statistics @type The new network type. By default, networkTypeOther
setNetworkType type:NetworkType = Ok;

//@description Returns network data usage statistics. Can be called before authorization @only_current If true, returns only data for the current library launch
getNetworkStatistics only_current:Bool = NetworkStatistics;

//@description Adds the specified data to data usage statistics. Can be called before authorization @entry The network statistics entry with the data to be added to statistics
addNetworkStatistics entry:NetworkStatisticsEntry = Ok;

//@description Resets all network data usage statistics to zero. Can be called before authorization
resetNetworkStatistics = Ok;


//@description Returns one of the available Telegram Passport elements @type Telegram Passport element type @password Password of the current user
getPassportElement type:PassportElementType password:string = PassportElement;

//@description Returns all available Telegram Passport elements @password Password of the current user
getAllPassportElements password:string = PassportElements;

//@description Adds an element to the user's Telegram Passport. May return an error with a message "PHONE_VERIFICATION_NEEDED" or "EMAIL_VERIFICATION_NEEDED" if the chosen phone number or the chosen email address must be verified first @element Input Telegram Passport element @password Password of the current user
setPassportElement element:InputPassportElement password:string = PassportElement;

//@description Deletes a Telegram Passport element @type Element type
deletePassportElement type:PassportElementType = Ok;

//@description Informs the user that some of the elements in their Telegram Passport contain errors; for bots only. The user will not be able to resend the elements, until the errors are fixed @user_id User identifier @errors The errors
setPassportElementErrors user_id:int32 errors:vector<inputPassportElementError> = Ok;


//@description Returns an IETF language tag of the language preferred in the country, which should be used to fill native fields in Telegram Passport personal details. Returns a 404 error if unknown @country_code A two-letter ISO 3166-1 alpha-2 country code
getPreferredCountryLanguage country_code:string = Text;


//@description Sends a code to verify a phone number to be added to a user's Telegram Passport
//@phone_number The phone number of the user, in international format @allow_flash_call Pass true if the authentication code may be sent via flash call to the specified phone number @is_current_phone_number Pass true if the phone number is used on the current device. Ignored if allow_flash_call is false
sendPhoneNumberVerificationCode phone_number:string allow_flash_call:Bool is_current_phone_number:Bool = AuthenticationCodeInfo;

//@description Re-sends the code to verify a phone number to be added to a user's Telegram Passport
resendPhoneNumberVerificationCode = AuthenticationCodeInfo;

//@description Checks the phone number verification code for Telegram Passport @code Verification code
checkPhoneNumberVerificationCode code:string = Ok;


//@description Sends a code to verify an email address to be added to a user's Telegram Passport @email_address Email address
sendEmailAddressVerificationCode email_address:string = EmailAddressAuthenticationCodeInfo;

//@description Re-sends the code to verify an email address to be added to a user's Telegram Passport
resendEmailAddressVerificationCode = EmailAddressAuthenticationCodeInfo;

//@description Checks the email address verification code for Telegram Passport @code Verification code
checkEmailAddressVerificationCode code:string = Ok;


//@description Returns a Telegram Passport authorization form for sharing data with a service @bot_user_id User identifier of the service's bot @scope Telegram Passport element types requested by the service @public_key Service's public_key @nonce Authorization form nonce provided by the service
getPassportAuthorizationForm bot_user_id:int32 scope:string public_key:string nonce:string = PassportAuthorizationForm;

//@description Returns already available Telegram Passport elements suitable for completing a Telegram Passport authorization form. Result can be received only once for each authorization form @autorization_form_id Authorization form identifier @password Password of the current user
getPassportAuthorizationFormAvailableElements autorization_form_id:int32 password:string = PassportElementsWithErrors;

//@description Sends a Telegram Passport authorization form, effectively sharing data with the service. This method must be called after getPassportAuthorizationFormAvailableElements if some previously available elements need to be used
//@autorization_form_id Authorization form identifier @types Types of Telegram Passport elements chosen by user to complete the authorization form
sendPassportAuthorizationForm autorization_form_id:int32 types:vector<PassportElementType> = Ok;


//@description Sends phone number confirmation code. Should be called when user presses "https://t.me/confirmphone?phone=*******&hash=**********" or "tg://confirmphone?phone=*******&hash=**********" link @hash Value of the "hash" parameter from the link
//@phone_number Value of the "phone" parameter from the link @allow_flash_call Pass true if the authentication code may be sent via flash call to the specified phone number @is_current_phone_number Pass true if the phone number is used on the current device. Ignored if allow_flash_call is false
sendPhoneNumberConfirmationCode hash:string phone_number:string allow_flash_call:Bool is_current_phone_number:Bool = AuthenticationCodeInfo;

//@description Resends phone number confirmation code
resendPhoneNumberConfirmationCode = AuthenticationCodeInfo;

//@description Checks phone number confirmation code @code The phone number confirmation code
checkPhoneNumberConfirmationCode code:string = Ok;


//@description Informs the server about the number of pending bot updates if they haven't been processed for a long time; for bots only @pending_update_count The number of pending updates @error_message The last error message
setBotUpdatesStatus pending_update_count:int32 error_message:string = Ok;


//@description Uploads a PNG image with a sticker; for bots only; returns the uploaded file @user_id Sticker file owner @png_sticker PNG image with the sticker; must be up to 512 kB in size and fit in 512x512 square
uploadStickerFile user_id:int32 png_sticker:InputFile = File;

//@description Creates a new sticker set; for bots only. Returns the newly created sticker set @user_id Sticker set owner @title Sticker set title; 1-64 characters @name Sticker set name. Can contain only English letters, digits and underscores. Must end with *"_by_<bot username>"* (*<bot_username>* is case insensitive); 1-64 characters
//@is_masks True, if stickers are masks @stickers List of stickers to be added to the set
createNewStickerSet user_id:int32 title:string name:string is_masks:Bool stickers:vector<inputSticker> = StickerSet;

//@description Adds a new sticker to a set; for bots only. Returns the sticker set @user_id Sticker set owner @name Sticker set name @sticker Sticker to add to the set
addStickerToSet user_id:int32 name:string sticker:inputSticker = StickerSet;

//@description Changes the position of a sticker in the set to which it belongs; for bots only. The sticker set must have been created by the bot @sticker Sticker @position New position of the sticker in the set, zero-based
setStickerPositionInSet sticker:InputFile position:int32 = Ok;

//@description Removes a sticker from the set to which it belongs; for bots only. The sticker set must have been created by the bot @sticker Sticker
removeStickerFromSet sticker:InputFile = Ok;


//@description Returns information about a file with a map thumbnail in PNG format. Only map thumbnail files with size less than 1MB can be downloaded @location Location of the map center @zoom Map zoom level; 13-20 @width Map width in pixels before applying scale; 16-1024 @height Map height in pixels before applying scale; 16-1024 @scale Map scale; 1-3 @chat_id Identifier of a chat, in which the thumbnail will be shown. Use 0 if unknown
getMapThumbnailFile location:location zoom:int32 width:int32 height:int32 scale:int32 chat_id:int53 = File;


//@description Accepts Telegram terms of services @terms_of_service_id Terms of service identifier
acceptTermsOfService terms_of_service_id:string = Ok;


//@description Sends a custom request; for bots only @method The method name @parameters JSON-serialized method parameters
sendCustomRequest method:string parameters:string = CustomRequestResult;

//@description Answers a custom query; for bots only @custom_query_id Identifier of a custom query @data JSON-serialized answer to the query
answerCustomQuery custom_query_id:int64 data:string = Ok;


//@description Succeeds after a specified amount of time has passed. Can be called before authorization. Can be called before initialization @seconds Number of seconds before the function returns
setAlarm seconds:double = Ok;


//@description Uses current user IP to found his country. Returns two-letter ISO 3166-1 alpha-2 country code. Can be called before authorization
getCountryCode = Text;

//@description Returns the default text for invitation messages to be used as a placeholder when the current user invites friends to Telegram
getInviteText = Text;

//@description Returns information about a tg:// deep link. Use "tg://need_update_for_some_feature" or "tg:some_unsupported_feature" for testing. Returns a 404 error for unknown links. Can be called before authorization @link The link
getDeepLinkInfo link:string = DeepLinkInfo;


//@description Returns application config, provided by the server. Can be called before authorization
getApplicationConfig = JsonValue;

//@description Saves application log event on the server. Can be called before authorization @type Event type @chat_id Optional chat identifier, associated with the event @data The log event data
saveApplicationLogEvent type:string chat_id:int53 data:JsonValue = Ok;


//@description Adds a proxy server for network requests. Can be called before authorization @server Proxy server IP address @port Proxy server port @enable True, if the proxy should be enabled @type Proxy type
addProxy server:string port:int32 enable:Bool type:ProxyType = Proxy;

//@description Edits an existing proxy server for network requests. Can be called before authorization @proxy_id Proxy identifier @server Proxy server IP address @port Proxy server port @enable True, if the proxy should be enabled @type Proxy type
editProxy proxy_id:int32 server:string port:int32 enable:Bool type:ProxyType = Proxy;

//@description Enables a proxy. Only one proxy can be enabled at a time. Can be called before authorization @proxy_id Proxy identifier
enableProxy proxy_id:int32 = Ok;

//@description Disables the currently enabled proxy. Can be called before authorization
disableProxy = Ok;

//@description Removes a proxy server. Can be called before authorization @proxy_id Proxy identifier
removeProxy proxy_id:int32 = Ok;

//@description Returns list of proxies that are currently set up. Can be called before authorization
getProxies = Proxies;

//@description Returns an HTTPS link, which can be used to add a proxy. Available only for SOCKS5 and MTProto proxies. Can be called before authorization @proxy_id Proxy identifier
getProxyLink proxy_id:int32 = Text;

//@description Computes time needed to receive a response from a Telegram server through a proxy. Can be called before authorization @proxy_id Proxy identifier. Use 0 to ping a Telegram server without a proxy
pingProxy proxy_id:int32 = Seconds;


//@description Sets new log stream for internal logging of TDLib. This is an offline method. Can be called before authorization. Can be called synchronously @log_stream New log stream
setLogStream log_stream:LogStream = Ok;

//@description Returns information about currently used log stream for internal logging of TDLib. This is an offline method. Can be called before authorization. Can be called synchronously
getLogStream = LogStream;

//@description Sets the verbosity level of the internal logging of TDLib. This is an offline method. Can be called before authorization. Can be called synchronously
//@new_verbosity_level New value of the verbosity level for logging. Value 0 corresponds to fatal errors, value 1 corresponds to errors, value 2 corresponds to warnings and debug warnings, value 3 corresponds to informational, value 4 corresponds to debug, value 5 corresponds to verbose debug, value greater than 5 and up to 1023 can be used to enable even more logging
setLogVerbosityLevel new_verbosity_level:int32 = Ok;

//@description Returns current verbosity level of the internal logging of TDLib. This is an offline method. Can be called before authorization. Can be called synchronously
getLogVerbosityLevel = LogVerbosityLevel;

//@description Returns list of available TDLib internal log tags, for example, ["actor", "binlog", "connections", "notifications", "proxy"]. This is an offline method. Can be called before authorization. Can be called synchronously
getLogTags = LogTags;

//@description Sets the verbosity level for a specified TDLib internal log tag. This is an offline method. Can be called before authorization. Can be called synchronously
//@tag Logging tag to change verbosity level @new_verbosity_level New verbosity level; 1-1024
setLogTagVerbosityLevel tag:string new_verbosity_level:int32 = Ok;

//@description Returns current verbosity level for a specified TDLib internal log tag. This is an offline method. Can be called before authorization. Can be called synchronously @tag Logging tag to change verbosity level
getLogTagVerbosityLevel tag:string = LogVerbosityLevel;

//@description Adds a message to TDLib internal log. This is an offline method. Can be called before authorization. Can be called synchronously
//@verbosity_level Minimum verbosity level needed for the message to be logged, 0-1023 @text Text of a message to log
addLogMessage verbosity_level:int32 text:string = Ok;


//@description Does nothing; for testing only. This is an offline method. Can be called before authorization
testCallEmpty = Ok;
//@description Returns the received string; for testing only. This is an offline method. Can be called before authorization @x String to return
testCallString x:string = TestString;
//@description Returns the received bytes; for testing only. This is an offline method. Can be called before authorization @x Bytes to return
testCallBytes x:bytes = TestBytes;
//@description Returns the received vector of numbers; for testing only. This is an offline method. Can be called before authorization @x Vector of numbers to return
testCallVectorInt x:vector<int32> = TestVectorInt;
//@description Returns the received vector of objects containing a number; for testing only. This is an offline method. Can be called before authorization @x Vector of objects to return
testCallVectorIntObject x:vector<testInt> = TestVectorIntObject;
//@description Returns the received vector of strings; for testing only. This is an offline method. Can be called before authorization @x Vector of strings to return
testCallVectorString x:vector<string> = TestVectorString;
//@description Returns the received vector of objects containing a string; for testing only. This is an offline method. Can be called before authorization @x Vector of objects to return
testCallVectorStringObject x:vector<testString> = TestVectorStringObject;
//@description Returns the squared received number; for testing only. This is an offline method. Can be called before authorization @x Number to square
testSquareInt x:int32 = TestInt;
//@description Sends a simple network request to the Telegram servers; for testing only. Can be called before authorization
testNetwork = Ok;
//@description Forces an updates.getDifference call to the Telegram servers; for testing only
testGetDifference = Ok;
//@description Does nothing and ensures that the Update object is used; for testing only. This is an offline method. Can be called before authorization
testUseUpdate = Update;
//@description Does nothing and ensures that the Error object is used; for testing only. This is an offline method. Can be called before authorization
testUseError = Error;
