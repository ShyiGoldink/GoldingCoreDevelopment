add_subdirectory(third_party/InkingUIKit)
target_link_libraries(my_app PRIVATE inking::inking)
addexecute()