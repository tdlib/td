require 'tdlib-ruby'

TD.configure do |config|
  config.lib_path = 'path/to/dir_containing_lobtdjson'

  # You should obtain your own api_id and api_hash from https://my.telegram.org/apps
  config.client.api_id = 12345
  config.client.api_hash = '1234567890abcdefghigklmnopqrstuv'
end

TD::Api.set_log_verbosity_level(1)

client = TD::Client.new

begin
  state = nil

  client.on('updateAuthorizationState') do |update|
    next unless update.dig('authorization_state', '@type') == 'authorizationStateWaitPhoneNumber'
    state = :wait_phone
  end

  client.on('updateAuthorizationState') do |update|
    next unless update.dig('authorization_state', '@type') == 'authorizationStateWaitCode'
    state = :wait_code
  end

  client.on('updateAuthorizationState') do |update|
    next unless update.dig('authorization_state', '@type') == 'authorizationStateReady'
    state = :ready
  end

  loop do
    case state
    when :wait_phone
      p 'Please, enter your phone number:'
      phone = STDIN.gets.strip
      params = {
        '@type' => 'setAuthenticationPhoneNumber',
        'phone_number' => phone
      }
      client.broadcast_and_receive(params)
    when :wait_code
      p 'Please, enter code from SMS:'
      code = STDIN.gets.strip
      params = {
        '@type' => 'checkAuthenticationCode',
        'code' => code
      }
      client.broadcast_and_receive(params)
    when :ready
      @me = client.broadcast_and_receive('@type' => 'getMe')
      break
    end
  end

ensure
  client.close
end

p @me
