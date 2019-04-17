"""
Copyright (c) 2017-present, Facebook, Inc.

This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
"""
from django.urls import path

from . import views

urlpatterns = [
    path('', views.index),
    path('list/', views.list_replays),
    path('get/sc/', views.get_replay),
    path('get/cvis/', views.get_cvis),
]
