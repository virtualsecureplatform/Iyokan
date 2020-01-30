#!/usr/bin/ruby

require "shellwords"
require "open3"
require "pathname"

$has_any_error = false

##### utility #####

def quote(str, prefix = "> ")
  prefix + str.gsub("\n", "\n#{prefix}")
end

##### assert #####

def assert_regex(str, reg)
  raise "Assert failed: assert_regex #{reg.inspect}\n#{quote(str)}" unless str =~ reg
end

def check_code_detail(command, args)
  path = Pathname.new($path) / command
  res, _, code = Open3.capture3("#{Shellwords.join([path.to_s] + args)} 2>&1")
  raise "Unexpected status code: #{code}\n#{quote(res)}" if code != 0
  res
end

def print_error(fh, command, args, msg)
  $has_any_error = true
  fh.puts "\e[31m[ERROR] #{command.inspect} [#{args.map { |s| s.inspect }.join(", ")}]\e[m"
  fh.puts msg
  fh.puts
end

def check_code(command, args = [])
  check_code_detail(command, args)
rescue => ex
  print_error $stderr, "check_code", [command] + args, ex.message
end

def test_iyokan(args)
  res = check_code_detail "./iyokan", args
  yield res
rescue => ex
  print_error $stderr, "test_iyokan", args, ex.message
end

raise "ruby test.rb PATH [slow]" unless ARGV.size >= 1

$path = ARGV.shift
$path ||= ""

$SLOW_MODE_ENABLED = ARGV[0].nil? ? false : ARGV[0] == "slow"

##### test0 #####

check_code "./test0"
check_code "./test0", ["slow"] if $SLOW_MODE_ENABLED

##### iyokan #####

test_iyokan [
  "plain",
  "-l", "test/diamond-core.json",
  "-i", "test/kvsp_plain_req_packet00.in",
] do |r|
  assert_regex r, /f0\t1/
  assert_regex r, /x0\t42/
end

test_iyokan [
  "plain",
  "-l", "test/diamond-core-wo-rom.json",
  "-i", "test/kvsp_plain_req_packet00.in",
  "--enable-rom", "io_romAddr:7:io_romData:32",
] do |r|
  assert_regex r, /f0\t1/
  assert_regex r, /x0\t42/
end

exit 1 if $has_any_error
