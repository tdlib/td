./src.ps1 | ForEach-Object {
  echo $_
  clang-format -style=file -i $_
}