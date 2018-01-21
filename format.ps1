./src.ps1 | ForEach-Object {
  echo $_
  clang-format -verbose -style=file -i $_
}